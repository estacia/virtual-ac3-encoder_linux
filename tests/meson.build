core_test = executable(
  'vac3-core-test',
  'test_core.cpp',
  include_directories : include_directories('../src'),
)
test('vac3-core', core_test, timeout : 10)

ffmpeg_encoder_test = executable(
  'vac3-ffmpeg-encoder-test',
  'test_ffmpeg_encoder.cpp',
  dependencies : [avcodec_dep, avutil_dep],
)
test('vac3-ffmpeg-ac3-encode', ffmpeg_encoder_test, timeout : 20)
