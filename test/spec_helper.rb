require File.expand_path("./default_configuration", File.dirname(__FILE__))
require 'rubygems'
require 'erb'
require 'popen4'
require 'em-http'

RSpec.configure do |config|
  config.before(:each) do
    create_dirs
    config_log_and_pid_file
    default_configuration
    self.send(:configuration) if self.respond_to?(:configuration)
    @test_config_file = "#{method_name_for_test}.conf"

    create_config_file
    start_server unless @disable_start_stop_server
  end

  config.after(:each) do
    stop_server unless @disable_start_stop_server
    delete_config_and_log_files
  end

end

def test_configuration_result
  create_config_file
  stderr_msg = start_server
  stop_server
  return stderr_msg
end

def start_server
  error_message = ""
  status = POpen4::popen4("#{ nginx_executable } -c #{ config_filename }") do |stdout, stderr, stdin, pid|
    error_message = stderr.read.strip unless stderr.eof
    return error_message unless error_message.nil?
  end
  assert_equal(0, status.exitstatus, "Server doesn't started - #{error_message}")
end

def stop_server
  error_message = ""
  status = POpen4::popen4("#{ nginx_executable } -c #{ config_filename } -s stop") do |stdout, stderr, stdin, pid|
    error_message = stderr.read.strip unless stderr.eof
    return error_message unless error_message.nil?
  end
  assert_equal(0, status.exitstatus, "Server doesn't stop - #{error_message}")
end

def create_dirs
  FileUtils.mkdir(nginx_tests_tmp_dir) unless File.exist?(nginx_tests_tmp_dir) and File.directory?(nginx_tests_tmp_dir)
  FileUtils.mkdir("#{nginx_tests_tmp_dir}/client_body_temp") unless File.exist?("#{nginx_tests_tmp_dir}/client_body_temp") and File.directory?("#{nginx_tests_tmp_dir}/client_body_temp")
  FileUtils.mkdir("#{nginx_tests_tmp_dir}/logs") unless File.exist?("#{nginx_tests_tmp_dir}/logs") and File.directory?("#{nginx_tests_tmp_dir}/logs")
end

def config_log_and_pid_file
  @client_body_temp = File.expand_path("#{nginx_tests_tmp_dir}/client_body_temp")
  @pid_file = File.expand_path("#{nginx_tests_tmp_dir}/logs/nginx.pid")
  @main_error_log = File.expand_path("#{nginx_tests_tmp_dir}/logs/nginx-main_error-#{method_name_for_test}.log")
  @access_log = File.expand_path("#{nginx_tests_tmp_dir}/logs/nginx-http_access-#{method_name_for_test}.log")
  @error_log = File.expand_path("#{nginx_tests_tmp_dir}/logs/nginx-http_error-#{method_name_for_test}.log")
end

def delete_config_and_log_files
  if has_passed?
    File.delete(config_filename) if File.exist?(config_filename)
    File.delete(@main_error_log) if File.exist?(@main_error_log)
    File.delete(@access_log) if File.exist?(@access_log)
    File.delete(@error_log) if File.exist?(@error_log)
    FileUtils.rm_rf(@client_body_temp) if File.exist?(@client_body_temp)
  end
end

def create_config_file
  template = ERB.new @config_template || File.read("./nginx.conf.erb")
  config_content = template.result(binding)
  File.open(config_filename, 'w') {|f| f.write(config_content) }
end

def config_filename
  File.expand_path("#{nginx_tests_tmp_dir}/#{ @test_config_file }")
end

def method_name_for_test
  self.respond_to?(:example) ? sanitize_filename(self.example.full_description) : "test"
end

def has_passed?
  self.example.exception.nil? if self.respond_to?(:example) and self.example.instance_variable_defined?('@exception')
end

def nginx_executable
  return ENV['NGINX_EXEC'].nil? ? "/usr/local/nginx/sbin/nginx" : ENV['NGINX_EXEC']
end

def nginx_address
  return "http://#{nginx_host}:#{nginx_port}"
end

def nginx_host
  return ENV['NGINX_HOST'].nil? ? "127.0.0.1" : ENV['NGINX_HOST']
end

def nginx_port
  return ENV['NGINX_PORT'].nil? ? "9990" : ENV['NGINX_PORT']
end

def nginx_workers
  return ENV['NGINX_WORKERS'].nil? ? "1" : ENV['NGINX_WORKERS']
end

def nginx_tests_tmp_dir
  return ENV['NGINX_TESTS_TMP_DIR'].nil? ? "tmp" : ENV['NGINX_TESTS_TMP_DIR']
end

def sanitize_filename(filename)
  filename.strip.gsub(/^.*(\\|\/)/, '').gsub(/[^0-9A-Za-z.\-]/, '_')
end
