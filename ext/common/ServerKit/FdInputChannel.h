/*
 *  Phusion Passenger - https://www.phusionpassenger.com/
 *  Copyright (c) 2014 Phusion
 *
 *  "Phusion Passenger" is a trademark of Hongli Lai & Ninh Bui.
 *
 *  Permission is hereby granted, free of charge, to any person obtaining a copy
 *  of this software and associated documentation files (the "Software"), to deal
 *  in the Software without restriction, including without limitation the rights
 *  to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 *  copies of the Software, and to permit persons to whom the Software is
 *  furnished to do so, subject to the following conditions:
 *
 *  The above copyright notice and this permission notice shall be included in
 *  all copies or substantial portions of the Software.
 *
 *  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 *  IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 *  FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 *  AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 *  LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 *  OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 *  THE SOFTWARE.
 */
#ifndef _PASSENGER_SERVER_KIT_FD_INPUT_CHANNEL_H_
#define _PASSENGER_SERVER_KIT_FD_INPUT_CHANNEL_H_

#include <oxt/macros.hpp>
#include <boost/move/move.hpp>
#include <sys/types.h>
#include <unistd.h>
#include <ev.h>
#include <MemoryKit/mbuf.h>
#include <ServerKit/Context.h>
#include <ServerKit/Channel.h>

namespace Passenger {
namespace ServerKit {

using namespace oxt;


class FdInputChannel: protected Channel {
public:
	typedef Channel::Result (*DataCallback)(FdInputChannel *channel, const MemoryKit::mbuf &buffer, int errcode);

private:
	ev_io watcher;
	MemoryKit::mbuf buffer;

	static void _onReadable(EV_P_ ev_io *io, int revents) {
		static_cast<FdInputChannel *>(io->data)->onReadable(io, revents);
	}

	void onReadable(ev_io *io, int revents) {
		RefGuard guard(hooks, this);
		onReadableWithoutRefGuard();
	}

	void onReadableWithoutRefGuard() {
		unsigned int generation = this->generation;
		unsigned int i, origBufferSize;
		bool done = false;
		ssize_t ret;
		int e;

		for (i = 0; i < burstReadCount && !done; i++) {
			if (buffer.empty()) {
				buffer = MemoryKit::mbuf_get(&ctx->mbuf_pool);
			}

			origBufferSize = buffer.size();
			do {
				ret = ::read(watcher.fd, buffer.start, buffer.size());
			} while (OXT_UNLIKELY(ret == -1 && errno == EINTR));
			if (ret > 0) {
				MemoryKit::mbuf buffer2(buffer, 0, ret);
				if (size_t(ret) == size_t(buffer.size())) {
					// Unref mbuf_block
					buffer = MemoryKit::mbuf();
				} else {
					buffer = MemoryKit::mbuf(buffer, ret);
				}
				feedWithoutRefGuard(boost::move(buffer2));
				if (generation != this->generation) {
					// Callback deinitialized this object.
					return;
				}

				if (!acceptingInput()) {
					done = true;
					ev_io_stop(ctx->libev->getLoop(), &watcher);
					if (mayAcceptInputLater()) {
						consumedCallback = onChannelConsumed;
					}
				} else {
					// If we were unable to fill the entire buffer, then it's likely that
					// the client is slow and that the next read() will fail with
					// EAGAIN, so we stop looping and return to the event loop poller.
					done = (size_t) ret < origBufferSize;
				}

			} else if (ret == 0) {
				ev_io_stop(ctx->libev->getLoop(), &watcher);
				done = true;
				feedWithoutRefGuard(MemoryKit::mbuf());

			} else {
				e = errno;
				done = true;
				if (e != EAGAIN && e != EWOULDBLOCK) {
					ev_io_stop(ctx->libev->getLoop(), &watcher);
					feedError(e);
				}
			}
		}
	}

	static void onChannelConsumed(Channel *channel, unsigned int size) {
		FdInputChannel *self = static_cast<FdInputChannel *>(channel);
		self->consumedCallback = NULL;
		if (self->acceptingInput()) {
			ev_io_start(self->ctx->libev->getLoop(), &self->watcher);
		}
	}

	void initialize() {
		burstReadCount = 1;
		watcher.fd = -1;
		watcher.data = this;
	}

public:
	unsigned int burstReadCount;

	FdInputChannel() {
		initialize();
	}

	FdInputChannel(Context *context)
		: Channel(context)
	{
		initialize();
	}

	~FdInputChannel() {
		if (ctx != NULL) {
			ev_io_stop(ctx->libev->getLoop(), &watcher);
		}
	}

	// May only be called right after construction.
	void setContext(Context *context) {
		Channel::setContext(context);
	}

	void reinitialize(int fd) {
		Channel::reinitialize();
		ev_io_init(&watcher, _onReadable, fd, EV_READ);
	}

	void deinitialize() {
		buffer = MemoryKit::mbuf();
		ev_io_stop(ctx->libev->getLoop(), &watcher);
		watcher.fd = -1;
		consumedCallback = NULL;
		Channel::deinitialize();
	}

	// May only be called right after the constructor or reinitialize().
	void startReading() {
		startReadingInNextTick();
		onReadableWithoutRefGuard();
	}

	// May only be called right after the constructor or reinitialize().
	void startReadingInNextTick() {
		assert(Channel::acceptingInput());
		ev_io_start(ctx->libev->getLoop(), &watcher);
	}

	void start() {
		Channel::start();
	}

	void stop() {
		Channel::stop();
	}

	OXT_FORCE_INLINE
	int getFd() const {
		return watcher.fd;
	}

	void setDataCallback(DataCallback callback) {
		Channel::dataCallback = (Channel::DataCallback) callback;
	}

	OXT_FORCE_INLINE
	Hooks *getHooks() const {
		return hooks;
	}

	void setHooks(Hooks *hooks) {
		this->hooks = hooks;
	}
};


} // namespace ServerKit
} // namespace Passenger

#endif /* _PASSENGER_SERVER_KIT_FD_INPUT_CHANNEL_H_ */