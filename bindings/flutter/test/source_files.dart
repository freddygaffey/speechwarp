// Reads files of the repository, which only a test on a computer's Dart VM can do. In a browser it gives null.
export 'source_files_web.dart' if (dart.library.io) 'source_files_io.dart';
