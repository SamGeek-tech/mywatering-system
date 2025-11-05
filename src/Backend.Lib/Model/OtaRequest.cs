using System.Text.Json.Serialization;

namespace Backend.Lib.Model
{
    public class OtaRequest
    {
        [JsonPropertyName("version")]
        public string Version { get; set; } = string.Empty;

        [JsonPropertyName("url")]
        public string? Url { get; set; }

        [JsonPropertyName("checksum")]
        public string? Checksum { get; set; }

        [JsonPropertyName("notes")]
        public string? Notes { get; set; }
    }
}