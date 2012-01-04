def default_configuration
  @master_process = 'off'
  @daemon = 'off'

  @thumbextractor = "on"
  @video_filename = "$uri"
  @video_second = "$arg_second"
  @image_width = "$arg_width"
  @image_height = "$arg_height"

  @jpeg_baseline = nil
  @jpeg_progressive_mode = nil
  @jpeg_optimize = nil
  @jpeg_smooth = nil
  @jpeg_quality = nil
  @jpeg_dpi = nil
end
