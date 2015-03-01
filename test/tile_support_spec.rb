require File.expand_path("./spec_helper", File.dirname(__FILE__))
require 'net/http'
require 'uri'

describe "when composing a panel with multiple frames" do

  context "setting the number of cols" do
    it "should use as many rows as needed to show the video frames at each 5 seconds" do
      nginx_run_server(tile_cols: 2, only_keyframe: 'off') do
        content = image('/test_video.mp4?second=2&height=64', {}, "200")
        expect(content).to be_perceptual_equal_to('test_video_2_cols.jpg')
      end
    end

    context "and a sample interval value" do
      it "should use as many rows as needed to show the video frames at each 2 seconds" do
        nginx_run_server(tile_cols: 2, tile_sample_interval: 2, only_keyframe: 'off') do
          content = image('/test_video.mp4?second=2&height=64', {}, "200")
          expect(content).to be_perceptual_equal_to('test_video_2_cols_2s_interval.jpg')
        end
      end
    end

    context "and a max number of rows" do
      it "should use at most the given number of rows" do
        nginx_run_server(tile_cols: 2, tile_sample_interval: 2, tile_max_rows: 2, only_keyframe: 'off') do
          content = image('/test_video.mp4?second=2&height=64', {}, "200")
          expect(content).to be_perceptual_equal_to('test_video_2_cols_2s_interval_2_max_rows.jpg')
        end
      end
    end
  end

  context "setting the number of rows" do
    it "should use as many cols as needed to show the video frames at each 5 seconds" do
      nginx_run_server(tile_rows: 1, only_keyframe: 'off') do
        content = image('/test_video.mp4?second=2&height=64', {}, "200")
        expect(content).to be_perceptual_equal_to('test_video_1_rows.jpg')
      end
    end

    context "and a sample interval value" do
      it "should use as many cols as needed to show the video frames at each 2 seconds" do
        nginx_run_server(tile_rows: 1, tile_sample_interval: 2, only_keyframe: 'off') do
          content = image('/test_video.mp4?second=2&height=64', {}, "200")
          expect(content).to be_perceptual_equal_to('test_video_1_rows_2s_interval.jpg')
        end
      end
    end

    context "and a max number of cols" do
      it "should use at most the given number of cols" do
        nginx_run_server(tile_rows: 1, tile_sample_interval: 2, tile_max_cols: 5, only_keyframe: 'off') do
          content = image('/test_video.mp4?second=2&height=64', {}, "200")
          expect(content).to be_perceptual_equal_to('test_video_1_rows_2s_interval_5_max_cols.jpg')
        end
      end
    end
  end

  context "setting the number of cols and rows" do
    it "should use as many frames as possible to fill the grid" do
      nginx_run_server(tile_cols: 4, tile_rows: 4, only_keyframe: 'off') do
        content = image('/test_video.mp4?second=2&height=64', {}, "200")
        expect(content).to be_perceptual_equal_to('test_video_4_cols_4_rows.jpg')
      end
    end

    context "and a sample interval value" do
      it "should use as many frames as possible at each 3 seconds" do
        nginx_run_server(tile_cols: 4, tile_rows: 4, tile_sample_interval: 3, only_keyframe: 'off') do
          content = image('/test_video.mp4?second=2&height=64', {}, "200")
          expect(content).to be_perceptual_equal_to('test_video_4_cols_4_rows_3s_interval.jpg')
        end
      end
    end
  end

  context "setting the background color" do
    it "should fill empty frames with the given color" do
      nginx_run_server(tile_cols: 2, tile_color: '#EEAA33', only_keyframe: 'off') do
        content = image('/test_video.mp4?second=2&height=64', {}, "200")
        expect(content).to be_perceptual_equal_to('test_video_2_cols_bg_color.jpg')
      end
    end
  end

  context "setting a margin" do
    it "should surround the image with the given margin" do
      nginx_run_server(tile_cols: 2, tile_rows: 2, tile_margin: 5, tile_color: '#EEAA33', only_keyframe: 'off') do
        content = image('/test_video.mp4?second=2&height=64', {}, "200")
        expect(content).to be_perceptual_equal_to('test_video_2_cols_2_rows_5_margin.jpg')
      end
    end
  end

  context "setting a padding" do
    it "should apply the given padding to the frames" do
      nginx_run_server(tile_cols: 2, tile_rows: 2, tile_padding: 3, tile_color: '#EEAA33', only_keyframe: 'off') do
        content = image('/test_video.mp4?second=2&height=64', {}, "200")
        expect(content).to be_perceptual_equal_to('test_video_2_cols_2_rows_3_padding.jpg')
      end
    end
  end
end
