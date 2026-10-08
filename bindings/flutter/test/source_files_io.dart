import 'dart:io';

/// The text of a file at [path], relative to the package folder, which is where `flutter test` runs.
String? readSourceFile(String path) => File(path).readAsStringSync();
