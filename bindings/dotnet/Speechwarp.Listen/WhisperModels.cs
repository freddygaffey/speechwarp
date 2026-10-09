using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using Speechwarp.Transcription;

namespace Speechwarp.Listen;

/// <summary>
/// The whisper.cpp models an app can offer for download, from the whisper.cpp project on Hugging Face, with
/// their sizes and SHA-256 hashes. The app downloads the file it chooses (this package never uses the network)
/// and passes its path to <see cref="WhisperTranscriber"/>.
/// </summary>
/// <remarks>
/// The ".en" models understand English only and are a little more accurate at it; the others understand about a
/// hundred languages. The compressed (quantised, q5) models are well under half the size of the full ones, for a
/// little accuracy. Larger models are slower and more accurate: <see cref="TranscriptionModel.RelativeSpeed"/> gives
/// a rough idea of how much slower.
/// </remarks>
public static class WhisperModels
{
    private static readonly Lazy<IReadOnlyList<TranscriptionModel>> Catalogue = new(Read);
    private static readonly Lazy<IReadOnlyDictionary<string, string>> FileNames = new(ReadFileNames);

    /// <summary>Every model, ordered roughly from least to most accurate. The order may change; keep the id.</summary>
    public static IReadOnlyList<TranscriptionModel> All => Catalogue.Value;

    /// <summary>The model with this id, such as "whisper-base.en", or null.</summary>
    public static TranscriptionModel? Find(string id)
    {
        ArgumentNullException.ThrowIfNull(id);
        return All.FirstOrDefault(model => model.Id == id);
    }

    /// <summary>
    /// The name to save a catalogue model's download under, such as "ggml-base.en.bin", or null for a model not in
    /// the catalogue.
    /// </summary>
    public static string? FileName(TranscriptionModel model)
    {
        ArgumentNullException.ThrowIfNull(model);
        return FileNames.Value.TryGetValue(model.Id, out var name) ? name : null;
    }

    /// <summary>
    /// The catalogue model whose file name matches the file at <paramref name="path"/>, or, for any other whisper.cpp
    /// model file, a description of it: id "whisper-file:" and the file name, languages unknown (empty), no download,
    /// and <see cref="TranscriptionModel.RelativeSpeed"/> NaN.
    /// </summary>
    public static TranscriptionModel ForFile(string path)
    {
        ArgumentNullException.ThrowIfNull(path);
        var name = Path.GetFileName(path);
        foreach (var (id, fileName) in FileNames.Value)
        {
            if (string.Equals(fileName, name, StringComparison.OrdinalIgnoreCase))
                return Find(id)!;
        }
        long size = File.Exists(path) ? new FileInfo(path).Length : 0;
        return new TranscriptionModel($"whisper-file:{name}", TranscriptionEngine.Whisper, name, [], size, null, null,
            double.NaN);
    }

    private static unsafe IReadOnlyList<TranscriptionModel> Read()
    {
        int count = Native.speechwarp_listen_catalogue_count();
        var models = new List<TranscriptionModel>(count);
        for (int i = 0; i < count; i++)
        {
            var languages = Native.Text(Native.speechwarp_listen_catalogue_languages(i)) ?? "";
            models.Add(new TranscriptionModel(
                Native.Text(Native.speechwarp_listen_catalogue_id(i))!,
                TranscriptionEngine.Whisper,
                Native.Text(Native.speechwarp_listen_catalogue_name(i))!,
                languages.Split(',', StringSplitOptions.RemoveEmptyEntries | StringSplitOptions.TrimEntries),
                Native.speechwarp_listen_catalogue_size(i),
                Native.Text(Native.speechwarp_listen_catalogue_url(i)),
                Native.Text(Native.speechwarp_listen_catalogue_sha256(i)),
                Native.speechwarp_listen_catalogue_relative_speed(i)));
        }
        return models.AsReadOnly();
    }

    private static unsafe IReadOnlyDictionary<string, string> ReadFileNames()
    {
        int count = Native.speechwarp_listen_catalogue_count();
        var names = new Dictionary<string, string>(count);
        for (int i = 0; i < count; i++)
            names[Native.Text(Native.speechwarp_listen_catalogue_id(i))!] =
                Native.Text(Native.speechwarp_listen_catalogue_file_name(i))!;
        return names;
    }
}
