{
  "targets": [
    {
      "target_name": "dtmfin",
      'include_dirs': [
        'lib/libdtmf/src',
        'src',
        '<!@(pkg-config portaudio-2.0 --cflags-only-I | sed s/-I//g)'
      ],
      'libraries': [
          '<!@(pkg-config --libs-only-l portaudio-2.0)'
      ],
      'cflags': ['-std=c99'],
      "sources": [
        'lib/libdtmf/src/dtmfin.c',
        "src/node-dtmfin.cc"
      ]
    }
  ]
}
