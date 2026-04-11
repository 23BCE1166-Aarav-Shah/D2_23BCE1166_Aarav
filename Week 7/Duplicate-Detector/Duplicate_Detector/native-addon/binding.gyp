{
  "targets": [
    {
      "target_name": "duplicate_engine_addon",
      "sources": [
        "src/addon.cpp",
        "../src/config.cpp",
        "../src/database_cache.cpp",
        "../src/duplicate_engine.cpp",
        "../src/duplicate_detector.cpp",
        "../src/file_manager.cpp",
        "../src/file_scanner.cpp",
        "../src/hash_engine.cpp",
        "../src/job_manager.cpp",
        "../src/logger.cpp",
        "../src/preview_engine.cpp",
        "../src/watcher_service.cpp"
      ],
      "include_dirs": [
        "<!@(node -p \"require('node-addon-api').include\")",
        "../include",
        ".."
      ],
      "cflags_cc": [
        "-std=c++17",
        "-fexceptions",
        "-frtti",
        "<!(pkg-config --cflags opencv4)"
      ],
      "cflags": [
        "-fexceptions",
        "-frtti"
      ],
      "defines": [
        "NAPI_CPP_EXCEPTIONS"
      ],
      "dependencies": [
        "<!(node -p \"require('node-addon-api').gyp\")"
      ],
      "libraries": [
        "<!(pkg-config --libs opencv4 libavformat libavcodec libavutil libswresample libchromaprint)"
      ]
    }
  ]
}