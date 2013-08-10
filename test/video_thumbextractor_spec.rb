require File.expand_path("./spec_helper", File.dirname(__FILE__))
require 'net/http'
require 'uri'

describe "when getting a thumb" do

  describe "and module is enabled" do
    it "should return a image" do
      nginx_run_server do
        image('/test_video.mp4?second=2', {}, "200").should_not be_nil
      end
    end

    context "when the moov atom is at the end of the file" do
      it "should return a image equals to when it is at the beginning" do
        nginx_run_server do
          image_1 = image('/test_video_moov_atom_at_end.mp4?second=2')
          image_1.should eq(image('/test_video.mp4?second=2'))
        end
      end
    end

    context "when the video does not exists" do
      it "should return a 404" do
        nginx_run_server do
          image('/unexistent_video.mp4?second=2', {}, "404").should be_nil
        end
      end
    end

    context "when the requested second is on the end of the video" do
      context "and using only_keyframe with previous time" do
        it "should return a 200" do
          nginx_run_server(:only_keyframe => "on", :next_time => "off") do
            image('/test_video.mp4?second=12', {}, "200").should_not be_nil
          end
        end
      end

      context "and using only_keyframe with next time" do
        it "should return a 404" do
          nginx_run_server(:only_keyframe => "on", :next_time => "on") do
            image('/test_video.mp4?second=12', {}, "404").should be_nil
          end
        end
      end

      context "and using exact time" do
        it "should return a 200" do
          nginx_run_server(:only_keyframe => "off") do
            image('/test_video.mp4?second=12', {}, "200").should_not be_nil
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
          image('/test_video.mp4?second=2', {"Host" => 'proxied_server'}, "200").should_not be_nil
        end
      end

      context "when the moov atom is at the end of the file" do
        it "should return a image equals to when it is at the beginning" do
          nginx_run_server do
            image_1 = image('/test_video_moov_atom_at_end.mp4?second=2', {"Host" => 'proxied_server'})
            image_1.should eq(image('/test_video.mp4?second=2', {"Host" => 'proxied_server'}))
          end
        end
      end

      it "should return an image equals to a an image from the real video file" do
        nginx_run_server do
          image_1 = image('/test_video.mp4?second=2', {"Host" => 'proxied_server'})
          image_1.should eq(image('/test_video.mp4?second=2'))
        end
      end

      context "when the video does not exists" do
        it "should return a 404" do
          nginx_run_server do
            image('/unexistent_video.mp4?second=2', {"Host" => 'proxied_server'}, "404").should be_nil
          end
        end
      end
    end

    describe "and manipulating 'filename' configuration" do
      it "should accept complex values" do
        nginx_run_server(:video_filename => "$http_x_filename") do
          image('/?second=2', {"x-filename" => '/test_video.mp4'}, "200").should_not be_nil
        end
      end
    end

    describe "and manipulating 'second' configuration" do
      it "should accept complex values" do
        nginx_run_server(:video_second => "$arg_second$http_x_second") do
          image('/test_video.mp4', {"x-second" => 2}, "200").should_not be_nil
        end
      end

      it "should return 404 when second is greather than duration" do
        nginx_run_server(:video_second => "$arg_second$http_x_second") do
          image('/test_video.mp4', {"x-second" => 20}, "404").should be_nil
        end
      end

      it "should retturn different images for two different seconds" do
        nginx_run_server(:video_second => "$arg_second$http_x_second") do
          image_1 = image('/test_video.mp4?second=2')
          image_1.should_not eq(image('/test_video.mp4?second=6'))
        end
      end
    end

    describe "and manipulating 'width' configuration" do
      it "should accept complex values" do
        nginx_run_server(:image_width => "$arg_width$http_x_width") do
          image('/test_video.mp4?second=2', {"x-width" => 100}, "200").should_not be_nil
        end
      end

      it "should reject width less than 16px" do
        nginx_run_server(:image_width => "$arg_width$http_x_width") do
          image('/test_video.mp4?second=2', {"x-width" => 15}, "400").should be_nil
        end
      end

      it "should return an image with full size if zero value is given" do
        nginx_run_server(:image_width => "$arg_width$http_x_width") do
          image_1 = image('/test_video.mp4?second=2&width=0')
          image_1.should eq(image('/test_video.mp4?second=2'))
        end
      end

      it "should return an image with full size if negative value is given" do
        nginx_run_server(:image_width => "$arg_width$http_x_width") do
          image_1 = image('/test_video.mp4?second=2&width=-15')
          image_1.should eq(image('/test_video.mp4?second=2'))
        end
      end
    end

    describe "and manipulating 'height' configuration" do
      it "should accept complex values" do
        nginx_run_server(:image_height => "$arg_height$http_x_height") do
          image('/test_video.mp4?second=2', {"x-height" => 100}, "200").should_not be_nil
        end
      end

      it "should reject height less than 16px" do
        nginx_run_server(:image_height => "$arg_height$http_x_height") do
          image('/test_video.mp4?second=2', {"x-height" => 15}, "400").should be_nil
        end
      end

      it "should return an image with full size if zero value is given" do
        nginx_run_server(:image_height => "$arg_height$http_x_height") do
          image_1 = image('/test_video.mp4?second=2&height=0')
          image_1.should eq(image('/test_video.mp4?second=2'))
        end
      end

      it "should return an image with full size if negative value is given" do
        nginx_run_server(:image_height => "$arg_height$http_x_height") do
          image_1 = image('/test_video.mp4?second=2&height=-15')
          image_1.should eq(image('/test_video.mp4?second=2'))
        end
      end

      it "should return an image with relative size if only height is given" do
        nginx_run_server(:image_height => "$arg_height$http_x_height") do
          image_1 = image('/test_video.mp4?second=2&width=480&height=270')
          image_1.should eq(image('/test_video.mp4?second=2&height=270'))
        end
      end

      it "should return an image with specified size if height and width are given" do
        nginx_run_server(:image_height => "$arg_height$http_x_height") do
          image_1 = image('/test_video.mp4?second=2')
          image_1.should_not eq(image('/test_video.mp4?second=2&width=200&height=360'))
        end
      end
    end

    describe "and manipulating 'only_keyframes' and 'next_time' configurations" do
      let!(:configuration) do {
        :extra_location => %{

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
          image_1.should_not eq(image('/next/test_video.mp4?second=2'))
        end
      end

      it "should return different images if you use only_keyframes or not" do
        nginx_run_server(configuration) do
          image_1 = image('/exact/test_video.mp4?second=2')
          image_1.should_not eq(image('/next/test_video.mp4?second=2'))
        end
      end
    end
  end

  describe "and module is disabled" do
    it "should return a video" do
      nginx_run_server(:thumbextractor => nil) do
        EventMachine.run do
          req = EventMachine::HttpRequest.new(nginx_address + '/test_video.mp4?second=2').get :timeout => 10
          req.callback do
            req.response_header.status.should eq(200)
            req.response_header.content_length.should_not eq(0)
            req.response_header["CONTENT_TYPE"].should eq("video/mp4")

            EventMachine.stop
          end
        end
      end
    end
  end
end
