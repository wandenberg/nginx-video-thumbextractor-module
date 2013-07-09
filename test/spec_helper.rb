require 'rubygems'
# Set up gems listed in the Gemfile.
ENV['BUNDLE_GEMFILE'] ||= File.expand_path('Gemfile', File.dirname(__FILE__))
require 'bundler/setup' if File.exists?(ENV['BUNDLE_GEMFILE'])
Bundler.require(:default, :test) if defined?(Bundler)

require File.expand_path('nginx_configuration', File.dirname(__FILE__))

RSpec.configure do |config|
  config.before(:suite) do
    FileUtils.rm_rf Dir[File.join(NginxTestHelper.nginx_tests_tmp_dir, "cores", "**")]
  end
  config.before(:each) do
    FileUtils.mkdir_p File.join(File.join(NginxTestHelper.nginx_tests_tmp_dir, "cores", config_id))
  end
  config.after(:each) do
    NginxTestHelper::Config.delete_config_and_log_files(config_id) if has_passed?
  end
  config.order = "random"
  config.treat_symbols_as_metadata_keys_with_true_values = true
  config.run_all_when_everything_filtered = true
end

def image(url, headers={}, expected_status="200")
  url = URI.parse(nginx_address + url)
  the_request = Net::HTTP::Get.new(url.request_uri)
  headers.keys.each {|k| the_request.add_field(k, headers[k])}
  the_response = Net::HTTP.start(url.host, url.port) { |http| http.request(the_request) }
  the_response.code.should eq(expected_status)
  if the_response.code == "200"
    the_response.header.content_type.should eq("image/jpeg")
    the_response.body
  else
    the_response.header.content_type.should eq("text/html")
    nil
  end
end
