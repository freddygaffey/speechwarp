#if os(iOS) || os(macOS)
import Foundation
import Speechwarp
import speechwarp_listen

/// What can go wrong with whisper.cpp.
public enum WhisperError: Error, Equatable {
    /// The model given is not a whisper model (its engine is another).
    case notAWhisperModel(String)
    /// There is no file at this path.
    case modelFileMissing(String)
    /// The file at this path is not a whisper.cpp model, or memory ran out loading it.
    case invalidModelFile(String)
    /// `prepare` has not completed.
    case notPrepared
    /// Whisper does not know this language.
    case unknownLanguage(String)
    /// The sample rate is outside 4000 to 384000 Hz.
    case unsupportedSampleRate(Int)
    /// A second `finish` was called while one was running.
    case alreadyFinishing
    /// The session was closed while `finish` was running.
    case closed
    /// Memory ran out.
    case outOfMemory
    /// whisper.cpp failed, with the library's description.
    case engineFailed(String)

    /// The error for one of the library's error codes, other than a cancel.
    static func code(_ code: Int32) -> WhisperError {
        switch code {
        case SPEECHWARP_LISTEN_ERROR_MEMORY: return .outOfMemory
        default: return .engineFailed(String(cString: speechwarp_listen_error_message(code)))
        }
    }
}

/// The whisper.cpp models an app can offer for download, from the whisper.cpp project on Hugging Face, with their
/// sizes and SHA-256 hashes. The app downloads the file it chooses (this package never uses the network) and passes
/// its path to `WhisperTranscriber`.
///
/// The ".en" models understand English only and are a little more accurate at it; the others understand about a
/// hundred languages. The compressed (quantised, q5) models are well under half the size of the full ones, for a
/// little accuracy. Larger models are slower and more accurate: `relativeSpeed` gives a rough idea of how much
/// slower.
public enum WhisperModels {
    private static let catalogue: [(model: TranscriptionModel, fileName: String)] = {
        (0..<speechwarp_listen_catalogue_count()).map { index in
            let languages = String(cString: speechwarp_listen_catalogue_languages(index))
            let model = TranscriptionModel(
                id: String(cString: speechwarp_listen_catalogue_id(index)),
                engine: .whisper,
                name: String(cString: speechwarp_listen_catalogue_name(index)),
                languages: languages.split(separator: ",").map { $0.trimmingCharacters(in: .whitespaces) },
                sizeBytes: speechwarp_listen_catalogue_size(index),
                downloadURL: String(cString: speechwarp_listen_catalogue_url(index)),
                sha256: String(cString: speechwarp_listen_catalogue_sha256(index)),
                relativeSpeed: speechwarp_listen_catalogue_relative_speed(index))
            return (model, String(cString: speechwarp_listen_catalogue_file_name(index)))
        }
    }()

    /// Every model, ordered roughly from least to most accurate. The order may change; keep the id.
    public static var all: [TranscriptionModel] { catalogue.map(\.model) }

    /// The model with this id, such as "whisper-base.en", or nil.
    public static func find(_ id: String) -> TranscriptionModel? {
        catalogue.first { $0.model.id == id }?.model
    }

    /// The name to save a catalogue model's download under, such as "ggml-base.en.bin", or nil for a model not in
    /// the catalogue.
    public static func fileName(_ model: TranscriptionModel) -> String? {
        catalogue.first { $0.model.id == model.id }?.fileName
    }

    /// The catalogue model whose file name matches the file at `path`, or, for any other whisper.cpp model file, a
    /// description of it: id "whisper-file:" and the file name, languages unknown (empty), no download, and
    /// `relativeSpeed` NaN.
    public static func forFile(_ path: String) -> TranscriptionModel {
        let name = (path as NSString).lastPathComponent
        if let entry = catalogue.first(where: { $0.fileName.caseInsensitiveCompare(name) == .orderedSame }) {
            return entry.model
        }
        let size = (try? FileManager.default.attributesOfItem(atPath: path)[.size] as? NSNumber)?.int64Value ?? 0
        return TranscriptionModel(id: "whisper-file:\(name)", engine: .whisper, name: name, languages: [],
                                  sizeBytes: size, downloadURL: nil, sha256: nil, relativeSpeed: .nan)
    }
}
#endif
