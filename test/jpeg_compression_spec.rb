require File.expand_path("./spec_helper", File.dirname(__FILE__))
require 'net/http'
require 'uri'

describe "when compressing an image" do
  let!(:configuration) do
    @disable_start_stop_server = true
  end

  it "should return a different image from default when change jpeg_quality directive" do
    start_server
    default_image = image('/test_video.mp4?second=2')
    stop_server

    @jpeg_quality = 80

    create_config_file
    start_server
    changed_image = image('/test_video.mp4?second=2')
    stop_server

    changed_image.should_not eq(default_image)
  end

  it "should return a different image from default when change jpeg_dpi directive" do
    start_server
    default_image = image('/test_video.mp4?second=2')
    stop_server

    @jpeg_dpi = 80

    create_config_file
    start_server
    changed_image = image('/test_video.mp4?second=2')
    stop_server

    changed_image.should_not eq(default_image)
  end

  it "should return a different image from default when change jpeg_smooth directive" do
    start_server
    default_image = image('/test_video.mp4?second=2')
    stop_server

    @jpeg_smooth = 1

    create_config_file
    start_server
    changed_image = image('/test_video.mp4?second=2')
    stop_server

    changed_image.should_not eq(default_image)
  end

  it "should return a different image from default when change jpeg_progressive_mode directive" do
    start_server
    default_image = image('/test_video.mp4?second=2')
    stop_server

    @jpeg_progressive_mode = 1

    create_config_file
    start_server
    changed_image = image('/test_video.mp4?second=2')
    stop_server

    changed_image.should_not eq(default_image)
  end

end

def image(url)
  url = URI.parse(nginx_address + url)
  the_request = Net::HTTP::Get.new(url.request_uri)
  the_response = Net::HTTP.start(url.host, url.port) { |http| http.request(the_request) }
  the_response.body
end
