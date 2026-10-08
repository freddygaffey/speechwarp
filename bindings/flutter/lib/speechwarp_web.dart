import 'package:flutter_web_plugins/flutter_web_plugins.dart';

/// The web side of the plugin. There is nothing to register: the library is WebAssembly that the Dart code
/// loads itself (see `Speechwarp.initialize`). The class exists because Flutter's plugin system wants one for
/// every platform a plugin declares.
class SpeechwarpWeb {
  /// Called by the generated plugin registrant; does nothing.
  static void registerWith(Registrar registrar) {}
}
