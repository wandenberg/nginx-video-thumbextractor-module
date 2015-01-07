require 'rubygems'
# Set up gems listed in the Gemfile.
ENV['BUNDLE_GEMFILE'] ||= File.expand_path('Gemfile', File.dirname(__FILE__))
require 'bundler/setup' if File.exists?(ENV['BUNDLE_GEMFILE'])
Bundler.require(:default, :test) if defined?(Bundler)

require File.expand_path('nginx_configuration', File.dirname(__FILE__))

RSpec.configure do |config|
  config.after(:each) do
    NginxTestHelper::Config.delete_config_and_log_files(config_id) if has_passed?
  end
  config.order = "random"
  config.run_all_when_everything_filtered = true
end

def image(url, headers={}, expected_status="200")
  uri = URI.parse(nginx_address + url)
  the_response = Net::HTTP.start(uri.host, uri.port) do |http|
    http.read_timeout = 120
    http.get(uri.request_uri, headers)
  end

  expect(the_response.code).to eq(expected_status)
  if the_response.code == "200"
    expect(the_response.header.content_type).to eq("image/jpeg")
    the_response.body
  else
    expect(the_response.header.content_type).to eq("text/html")
    nil
  end
end
