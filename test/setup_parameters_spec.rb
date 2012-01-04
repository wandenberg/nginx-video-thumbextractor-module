require File.expand_path("./spec_helper", File.dirname(__FILE__))

describe "when checking configuration" do
  let!(:configuration) do
    @disable_start_stop_server = true
  end

  describe "and module is enabled" do

    it "should reject if video_filename is not present" do
      expected_error_message = "video thumbextractor module: video_thumbextractor_video_filename must be defined"
      @video_filename = nil

      test_configuration_result.should include(expected_error_message)
    end

    it "should reject if video_second is not present" do
      expected_error_message = "video thumbextractor module: video_thumbextractor_video_second must be defined"
      @video_second = nil

      test_configuration_result.should include(expected_error_message)
    end

  end

  describe "and module is disabled" do
    it "should not validate module directives" do
      @video_filename = nil
      @video_second = nil

      @thumbextractor = nil

      test_configuration_result.should be_empty
    end
  end

  describe "and use non required directives" do
    it "should accept image_width" do
      @image_width = "$arg_width"
      test_configuration_result.should be_empty
    end

    it "should accept image_height" do
      @image_height = "$arg_height"
      test_configuration_result.should be_empty
    end

    it "should accept jpeg_baseline" do
      @jpeg_baseline = "1"
      test_configuration_result.should be_empty
    end

    it "should accept jpeg_progressive_mode" do
      @jpeg_progressive_mode = "1"
      test_configuration_result.should be_empty
    end

    it "should accept jpeg_optimize" do
      @jpeg_optimize = "1"
      test_configuration_result.should be_empty
    end

    it "should accept jpeg_smooth" do
      @jpeg_smooth = "1"
      test_configuration_result.should be_empty
    end

    it "should accept jpeg_quality" do
      @jpeg_quality = "1"
      test_configuration_result.should be_empty
    end

    it "should accept jpeg_dpi" do
      @jpeg_dpi = "1"
      test_configuration_result.should be_empty
    end

  end

end
