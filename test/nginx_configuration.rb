module NginxConfiguration
  def self.default_configuration
    {
      disable_start_stop_server: false,
      master_process: 'on',
      daemon: 'on',
      thumbextractor: "on",
      video_filename: "$uri",
      video_second: "$arg_second",
      image_width: "$arg_width",
      image_height: "$arg_height",

      only_keyframe: "on",
      next_time: "on",

      jpeg_baseline: nil,
      jpeg_progressive_mode: nil,
      jpeg_optimize: nil,
      jpeg_smooth: nil,
      jpeg_quality: nil,
      jpeg_dpi: nil,

      extra_location: nil
    }
  end

  def self.template_configuration
    File.open(File.expand_path('nginx.conf.erb', File.dirname(__FILE__))).read
  end
end
