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

RSpec::Matchers.define :be_perceptual_equal_to do |expected|
  match do |actual|
    (Pixmap.from_jpeg_buffer(actual) - Pixmap.from_jpeg_file(expected)).percentage_pixels_non_zero < 1
  end

  failure_message do |actual|
    "expected that #{actual} would be 99% equals to #{expected}"
  end

  failure_message_when_negated do |actual|
    "expected that #{actual} would be 99% different from #{expected}"
  end

  description do
    "be 99% equals"
  end
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

class Pixmap
  def initialize(width, height)
    @width = width
    @height = height
    @data = fill(RGBColour::WHITE)
  end

  attr_reader :width, :height

  def fill(colour)
    @data = Array.new(@width) {Array.new(@height, colour)}
  end

  def [](x, y)
    validate_pixel(x,y)
    @data[x][y]
  end
  alias_method :get_pixel, :[]

  def []=(x, y, colour)
    validate_pixel(x,y)
    @data[x][y] = colour
  end
  alias_method :set_pixel, :[]=

  def each_pixel
    if block_given?
      @height.times {|y| @width.times {|x| yield x,y }}
    else
      to_enum(:each_pixel)
    end
  end

  # the difference between two images
  def -(a_pixmap)
    if @width != a_pixmap.width or @height != a_pixmap.height
      raise ArgumentError, "can't compare images with different sizes"
    end

    bitmap = self.class.new(@width, @height)
    bitmap.each_pixel do |x, y|
      bitmap[x, y] = self[x, y] - a_pixmap[x, y]
    end
    bitmap
  end

  def percentage_pixels_non_zero
    sum = 0
    each_pixel {|x,y| sum += self[x, y].values.inject(0) {|colors_sum, val| colors_sum + val }}
    100.0 * Float(sum) / (@width * @height * 255 * 3)
  end

  def self.from_jpeg_file(filename)
    unless File.readable?(filename)
      raise ArgumentError, "#{filename} does not exists or is not readable."
    end

    jpeg_to_pixmap(Jpeg.open(filename))
  end

  def self.from_jpeg_buffer(buffer)
    jpeg_to_pixmap(Jpeg.open_buffer(buffer))
  end

  private

  def self.jpeg_to_pixmap(jpeg)
    width = jpeg.width
    height = jpeg.height

    if width < 1 || height < 1
      raise StandardError, "file '#{filename}' does not start with the expected header"
    end

    raw_data = jpeg.raw_data
    bitmap = self.new(width, height)
    bitmap.each_pixel do |x,y|
      values = raw_data[y][x]
      if jpeg.rgb?
        bitmap[x,y] = RGBColour.new(values[0], values[1], values[2])
      else
        bitmap[x,y] = RGBColour.new(values[0], values[0], values[0])
      end
    end
    bitmap
  end

  def validate_pixel(x,y)
    unless x.between?(0, @width - 1) && y.between?(0, @height - 1)
      raise ArgumentError, "requested pixel (#{x}, #{y}) is outside dimensions of this bitmap"
    end
  end
end

class RGBColour
  # Red, green and blue values must fall in the range 0..255.
  def initialize(red, green, blue)
    ok = [red, green, blue].inject(true) {|ret, c| ret &= c.between?(0,255)}
    raise ArgumentError, "invalid RGB parameters: #{[red, green, blue].inspect}" unless ok
    @red, @green, @blue = red, green, blue
  end

  attr_reader :red, :green, :blue
  alias_method :r, :red
  alias_method :g, :green
  alias_method :b, :blue

  def values
    [@red, @green, @blue]
  end

  def -(a_colour)
    self.class.new((@red - a_colour.red).abs, (@green - a_colour.green).abs, (@blue - a_colour.blue).abs)
  end

  WHITE = RGBColour.new(255, 255, 255)
end
