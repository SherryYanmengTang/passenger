#  Phusion Passenger - https://www.phusionpassenger.com/
#  Copyright (c) 2010-2015 Phusion Holding B.V.
#
#  "Passenger", "Phusion Passenger" and "Union Station" are registered
#  trademarks of Phusion Holding B.V.
#
#  See LICENSE file for license information.

TEST_BOOST_OXT_LIBRARY = LIBBOOST_OXT
TEST_COMMON_LIBRARY    = COMMON_LIBRARY
TEST_COMMON_CFLAGS     = "-DTESTING_APPLICATION_POOL"

desc "Run all unit tests and integration tests"
task :test => ['test:oxt', 'test:cxx', 'test:ruby', 'test:node', 'test:integration']

desc "Clean all compiled test files"
task 'test:clean' do
  sh("rm -rf #{TEST_OUTPUT_DIR}")
  sh("rm -f test/cxx/*.gch")
end

task :clean => 'test:clean'

file "#{TEST_OUTPUT_DIR}allocate_memory" => 'test/support/allocate_memory.c' do
  compile_c("#{TEST_OUTPUT_DIR}allocate_memory.o", 'test/support/allocate_memory.c')
  create_c_executable("#{TEST_OUTPUT_DIR}allocate_memory", "#{TEST_OUTPUT_DIR}allocate_memory.o")
end

desc "Install developer dependencies"
task 'test:install_deps' do
  gem_install = PlatformInfo.gem_command + " install --no-rdoc --no-ri"
  gem_install = "#{PlatformInfo.ruby_sudo_command} #{gem_install}" if boolean_option('SUDO')
  default = boolean_option('DEVDEPS_DEFAULT', true)
  install_base_deps = boolean_option('BASE_DEPS', default)
  install_doctools  = boolean_option('DOCTOOLS', default)

  if deps_target = string_option('DEPS_TARGET')
    bundle_args = "--path #{deps_target} #{ENV['BUNDLE_ARGS']}".strip
  else
    bundle_args = ENV['BUNDLE_ARGS'].to_s
  end

  if !PlatformInfo.locate_ruby_tool('bundle') || bundler_too_old?
    sh "gem uninstall bundler bundle rubygems-update --all -x && gem update --system 2.6.1 && gem install bundler && gem uninstall rubygems-update --all -x && gem update --system 2.6.4"
  end

  if install_base_deps && install_doctools
    sh "bundle install #{bundle_args} --without="
  else
    if install_base_deps
      sh "bundle install #{bundle_args} --without doc"
    end
    if install_doctools
      sh "bundle install #{bundle_args} --without base"
    end
  end
  
  if install_doctools
    # workaround for issue "bluecloth not found" when using 1.12.x
    sh "#{gem_install} bundler --version 1.11.2"
    sh "rvm list"
  end

  if boolean_option('USH_BUNDLES', default)
    sh "cd src/ruby_supportlib/phusion_passenger/vendor/union_station_hooks_core" \
      " && bundle install #{bundle_args} --with travis --without doc notravis"
    sh "cd src/ruby_supportlib/phusion_passenger/vendor/union_station_hooks_rails" \
      " && bundle install #{bundle_args} --without doc notravis"
    sh "cd src/ruby_supportlib/phusion_passenger/vendor/union_station_hooks_rails" \
      " && bundle exec rake install_test_app_bundles" \
      " BUNDLE_ARGS='#{bundle_args}'"
  end
  if boolean_option('NODE_MODULES', default)
    sh "npm install"
  end
end


def bundler_too_old?
  `bundle --version` =~ /version (.+)/
  version = $1.split('.').map { |x| x.to_i }
  version[0] < 1 || version[0] == 1 && version[1] < 10
end
