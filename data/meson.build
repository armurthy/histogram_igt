image_files = [
  '1080p-left.png',
  '1080p-right.png',
  'pass.png',
  'dpst_colorbars.png',
  'dpst_fuschia.png',
  'dpst_leaf.png',
  'dpst_wall.png',
  'dpst_washout.png',
  'dpst_white000.png',
  'dpst_white050.png',
  'dpst_white080.png',
  'dpst_white100.png',
  'dpst_winxp.png',
  'dpst_youtube.png',
]

foreach img : image_files
      configure_file(output:img, input:img, copy:true)
endforeach

install_data(sources : image_files, install_dir : datadir)
