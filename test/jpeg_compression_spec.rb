require File.expand_path("./spec_helper", File.dirname(__FILE__))
require 'net/http'
require 'uri'

describe "when compressing an image" do
  let!(:default_image) do
    nginx_run_server do
      image('/test_video.mp4?second=2')
    end
  end

  it "should return a different image from default when change jpeg_quality directive" do
    nginx_run_server(:jpeg_quality => 80) do
      image('/test_video.mp4?second=2').should_not eq(default_image)
    end
  end

  it "should return a different image from default when change jpeg_dpi directive" do
    nginx_run_server(:jpeg_dpi => 80) do
      image('/test_video.mp4?second=2').should_not eq(default_image)
    end
  end

  it "should return a different image from default when change jpeg_smooth directive" do
    nginx_run_server(:jpeg_smooth => 1) do
      image('/test_video.mp4?second=2').should_not eq(default_image)
    end
  end

  it "should return a different image from default when change jpeg_progressive_mode directive" do
    nginx_run_server(:jpeg_progressive_mode => 1) do
      image('/test_video.mp4?second=2').should_not eq(default_image)
    end
  end
end
