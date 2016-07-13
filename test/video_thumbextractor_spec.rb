require File.expand_path("./spec_helper", File.dirname(__FILE__))
require 'net/http'
require 'uri'

describe "when getting a thumb" do

  describe "and module is enabled" do
    it "should return a image" do
      nginx_run_server do
        content = image('/test_video.mp4?second=2', {}, "200")
        expect(content).to be_perceptual_equal_to('test_video_640_x_360.jpg')
      end
    end

    context "when the moov atom is at the end of the file" do
      it "should return a image equals to when it is at the beginning" do
        nginx_run_server do
          image_1 = image('/test_video_moov_atom_at_end.mp4?second=2')
          expect(image_1).to eq(image('/test_video.mp4?second=2'))
          expect(IO.binread('test_video.mp4')).not_to eq(IO.binread('test_video_moov_atom_at_end.mp4'))
        end
      end
    end

    context "when the video does not exists" do
      it "should return a 404" do
        nginx_run_server do
          expect(image('/unexistent_video.mp4?second=2', {}, "404")).to be_nil
        end
      end
    end

    context "when the video has multiple video streams" do
      it "should use the best video stream available" do
        nginx_run_server(only_keyframe: 'off') do
          content = image('/test_video_multiple_video_streams.mp4?second=2&height=90', {}, "200")
          expect(content).to be_perceptual_equal_to('test_video_multiple_video_streams.jpg')
        end
      end
    end

    context "when the video has sample aspect ratio less than 1" do
      it "should use the display dimensions as true dimensions" do
        nginx_run_server(only_keyframe: 'off') do
          content = image('/test_video_aspect_lt_1.mp4?second=2&height=90', {}, "200")
          expect(content).to be_perceptual_equal_to('test_video_aspect_lt_1.jpg')
        end
      end
    end

    context "when the video has sample aspect ratio greater than 1" do
      it "should use the display dimensions as true dimensions" do
        nginx_run_server(only_keyframe: 'off') do
          content = image('/test_video_aspect_gt_1.mp4?second=2&height=90', {}, "200")
          expect(content).to be_perceptual_equal_to('test_video_aspect_gt_1.jpg')
        end
      end
    end

    context "when the video stream has rotate metadata" do
      it "should rotate by 90 degrees when asked for" do
        nginx_run_server(only_keyframe: 'off') do
          content = image('/test_video_rotate_90.mp4?second=2&height=56', {}, "200")
          expect(content).to be_perceptual_equal_to('test_video_rotate_90.jpg', 98.3)
        end
      end

      it "should rotate by 180 degrees when asked for" do
        nginx_run_server(only_keyframe: 'off') do
          content = image('/test_video_rotate_180.mp4?second=2&height=56', {}, "200")
          expect(content).to be_perceptual_equal_to('test_video_rotate_180.jpg')
        end
      end

      it "should rotate by 270 degrees when asked for" do
        nginx_run_server(only_keyframe: 'off') do
          content = image('/test_video_rotate_270.mp4?second=2&height=56', {}, "200")
          expect(content).to be_perceptual_equal_to('test_video_rotate_270.jpg')
        end
      end
    end

    context "when the requested second is on the end of the video" do
      context "and using only_keyframe with previous time" do
        it "should return a 200" do
          nginx_run_server(only_keyframe: "on", next_time: "off") do
            expect(image('/test_video.mp4?second=12', {}, "200")).not_to be_nil
          end
        end
      end

      context "and using only_keyframe with next time" do
        it "should return a 404" do
          nginx_run_server(only_keyframe: "on", next_time: "on") do
            expect(image('/test_video.mp4?second=12', {}, "404")).to be_nil
          end
        end
      end

      context "and using exact time" do
        it "should return a 200" do
          nginx_run_server(only_keyframe: "off") do
            expect(image('/test_video.mp4?second=12', {}, "200")).not_to be_nil
          end
        end
      end
    end

    describe "and getting an image from a proxy cache file" do
      before do
        FileUtils.rm_r File.join(NginxTestHelper.nginx_tests_tmp_dir, "cache")
      end

      it "should return a image" do
        nginx_run_server do
          expect(image('/test_video.mp4?second=2', {"Host" => 'proxied_server'}, "200")).not_to be_nil
        end
      end

      context "when the moov atom is at the end of the file" do
        it "should return a image equals to when it is at the beginning" do
          nginx_run_server do
            image_1 = image('/test_video_moov_atom_at_end.mp4?second=2', {"Host" => 'proxied_server'})
            expect(image_1).to eq(image('/test_video.mp4?second=2', {"Host" => 'proxied_server'}))
            expect(IO.binread('test_video.mp4')).not_to eq(IO.binread('test_video_moov_atom_at_end.mp4'))
          end
        end
      end

      it "should return an image equals to a an image from the real video file" do
        nginx_run_server do
          image_1 = image('/test_video.mp4?second=2', {"Host" => 'proxied_server'})
          expect(image_1).to eq(image('/test_video.mp4?second=2'))
        end
      end

      context "when the video does not exists" do
        it "should return a 404" do
          nginx_run_server do
            expect(image('/unexistent_video.mp4?second=2', {"Host" => 'proxied_server'}, "404")).to be_nil
          end
        end
      end
    end

    describe "and manipulating 'filename' configuration" do
      it "should accept complex values" do
        nginx_run_server(video_filename: "$http_x_filename") do
          expect(image('/?second=2', {"x-filename" => '/test_video.mp4'}, "200")).not_to be_nil
        end
      end
    end

    describe "and manipulating 'second' configuration" do
      it "should accept complex values" do
        nginx_run_server(video_second: "$arg_second$http_x_second") do
          expect(image('/test_video.mp4', {"x-second" => "2"}, "200")).not_to be_nil
        end
      end

      it "should return 404 when second is greather than duration" do
        nginx_run_server(video_second: "$arg_second$http_x_second") do
          expect(image('/test_video.mp4', {"x-second" => "20"}, "404")).to be_nil
        end
      end

      it "should retturn different images for two different seconds" do
        nginx_run_server(video_second: "$arg_second$http_x_second") do
          image_1 = image('/test_video.mp4?second=2')
          expect(image_1).not_to eq(image('/test_video.mp4?second=6'))
        end
      end
    end

    describe "and manipulating 'width' configuration" do
      it "should accept complex values" do
        nginx_run_server(image_width: "$arg_width$http_x_width") do
          expect(image('/test_video.mp4?second=2', {"x-width" => "100"}, "200")).not_to be_nil
        end
      end

      it "should reject width less than 16px" do
        nginx_run_server(image_width: "$arg_width$http_x_width") do
          expect(image('/test_video.mp4?second=2', {"x-width" => "15"}, "400")).to be_nil
        end
      end

      it "should return an image with full size if zero value is given" do
        nginx_run_server(image_width: "$arg_width$http_x_width") do
          image_1 = image('/test_video.mp4?second=2&width=0')
          expect(image_1).to eq(image('/test_video.mp4?second=2'))
        end
      end

      it "should return an image with full size if negative value is given" do
        nginx_run_server(image_width: "$arg_width$http_x_width") do
          image_1 = image('/test_video.mp4?second=2&width=-15')
          expect(image_1).to eq(image('/test_video.mp4?second=2'))
        end
      end

      it "should ignore width if only this dimension is specified" do
        nginx_run_server do
          content = image('/test_video.mp4?second=2&width=100', {}, "200")
          expect(content).to be_perceptual_equal_to('test_video_640_x_360.jpg')
        end
      end
    end

    describe "and manipulating 'height' configuration" do
      it "should accept complex values" do
        nginx_run_server(image_height: "$arg_height$http_x_height") do
          expect(image('/test_video.mp4?second=2', {"x-height" => "100"}, "200")).not_to be_nil
        end
      end

      it "should reject height less than 16px" do
        nginx_run_server(image_height: "$arg_height$http_x_height") do
          expect(image('/test_video.mp4?second=2', {"x-height" => "15"}, "400")).to be_nil
        end
      end

      it "should return an image with full size if zero value is given" do
        nginx_run_server(image_height: "$arg_height$http_x_height") do
          image_1 = image('/test_video.mp4?second=2&height=0')
          expect(image_1).to eq(image('/test_video.mp4?second=2'))
        end
      end

      it "should return an image with full size if negative value is given" do
        nginx_run_server(image_height: "$arg_height$http_x_height") do
          image_1 = image('/test_video.mp4?second=2&height=-15')
          expect(image_1).to eq(image('/test_video.mp4?second=2'))
        end
      end

      it "should return an image with relative size if only height is given" do
        nginx_run_server(image_height: "$arg_height$http_x_height") do
          content = image('/test_video.mp4?second=2&height=270')
          expect(content).to be_perceptual_equal_to('test_video_480_x_270.jpg')
        end
      end

      it "should return an image with specified size if height and width are given" do
        nginx_run_server(image_height: "$arg_height$http_x_height") do
          content = image('/test_video.mp4?second=2&width=200&height=60')
          expect(content).to be_perceptual_equal_to('test_video_200_x_60.jpg')
        end
      end

      it "should accept a different aspect ratio" do
        nginx_run_server(image_height: "$arg_height$http_x_height") do
          content = image('/test_video.mp4?second=2&width=50&height=100')
          expect(content).to be_perceptual_equal_to('test_video_50_x_100.jpg')
        end
      end
    end

    describe "and manipulating 'only_keyframes' and 'next_time' configurations" do
      let!(:configuration) do {
        extra_location: %{

    location /previous {
      rewrite "^/previous(.*)" $1 break;

      video_thumbextractor;
      video_thumbextractor_video_filename        $uri;
      video_thumbextractor_video_second          $arg_second;
      video_thumbextractor_next_time off;

      root #{ File.expand_path(File.dirname(__FILE__)) };
    }

    location /next {
      rewrite "^/next(.*)" $1 break;

      video_thumbextractor;
      video_thumbextractor_video_filename        $uri;
      video_thumbextractor_video_second          $arg_second;

      root #{ File.expand_path(File.dirname(__FILE__)) };
    }

    location /exact {
      rewrite "^/exact(.*)" $1 break;

      video_thumbextractor;
      video_thumbextractor_video_filename        $uri;
      video_thumbextractor_video_second          $arg_second;
      video_thumbextractor_only_keyframe off;

      root #{ File.expand_path(File.dirname(__FILE__)) };
    }
        }
        }
      end

      it "should return different images if using next_time or not" do
        nginx_run_server(configuration) do
          image_1 = image('/previous/test_video.mp4?second=2')
          expect(image_1).not_to eq(image('/next/test_video.mp4?second=2'))
        end
      end

      it "should return different images if you use only_keyframes or not" do
        nginx_run_server(configuration) do
          image_1 = image('/exact/test_video.mp4?second=2')
          expect(image_1).not_to eq(image('/next/test_video.mp4?second=2'))
        end
      end
    end
  end

  describe "and module is disabled" do
    it "should return a video" do
      nginx_run_server(thumbextractor: nil) do
        EventMachine.run do
          req = EventMachine::HttpRequest.new(nginx_address + '/test_video.mp4?second=2').get timeout: 10
          req.callback do
            expect(req.response_header.status).to eq(200)
            expect(req.response_header.content_length).not_to eq(0)
            expect(req.response_header["CONTENT_TYPE"]).to eq("video/mp4")

            EventMachine.stop
          end
        end
      end
    end
  end
end
