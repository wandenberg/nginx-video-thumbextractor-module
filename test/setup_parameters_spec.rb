require File.expand_path("./spec_helper", File.dirname(__FILE__))

describe "when checking configuration" do
  describe "and module is enabled" do

    it "should reject if video_filename is not present" do
      expect(nginx_test_configuration(video_filename: nil)).to include "video thumbextractor module: video_thumbextractor_video_filename must be defined"
    end

    it "should reject if video_second is not present" do
      expect(nginx_test_configuration(video_second: nil)).to include "video thumbextractor module: video_thumbextractor_video_second must be defined"
    end

  end

  describe "and module is disabled" do
    it "should not validate module directives" do
      config = {
        thumbextractor: nil,
        video_filename: nil,
        video_second: nil
      }
      expect(nginx_test_configuration(config)).not_to include "video thumbextractor module:"
    end
  end

  describe "and use non required directives" do
    it "should accept image_width" do
      expect(nginx_test_configuration(image_width: "$arg_width")).not_to include "video thumbextractor module:"
    end

    it "should accept image_height" do
      expect(nginx_test_configuration(image_height: "$arg_height")).not_to include "video thumbextractor module:"
    end

    it "should accept jpeg_baseline" do
      expect(nginx_test_configuration(jpeg_baseline: "1")).not_to include "video thumbextractor module:"
    end

    it "should accept jpeg_progressive_mode" do
      expect(nginx_test_configuration(jpeg_progressive_mode: "1")).not_to include "video thumbextractor module:"
    end

    it "should accept jpeg_optimize" do
      expect(nginx_test_configuration(jpeg_optimize: "1")).not_to include "video thumbextractor module:"
    end

    it "should accept jpeg_smooth" do
      expect(nginx_test_configuration(jpeg_smooth: "1")).not_to include "video thumbextractor module:"
    end

    it "should accept jpeg_quality" do
      expect(nginx_test_configuration(jpeg_quality: "1")).not_to include "video thumbextractor module:"
    end

    it "should accept jpeg_dpi" do
      expect(nginx_test_configuration(jpeg_dpi: "1")).not_to include "video thumbextractor module:"
    end
  end
end
