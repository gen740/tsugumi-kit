#include "card.hh"

#include <charconv>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

namespace {

using Tsugumi::Data::FailureKind;
constexpr std::size_t kPreviewRows = 10;

auto ReadLine(std::ifstream& input, std::string& line) -> bool {
  if (std::getline(input, line)) {
    if (line.ends_with('\r')) line.pop_back();
    return true;
  }
  if (input.bad() || !input.eof()) {
    throw tsugumi::Failure(FailureKind::invalid_request, "Cannot read counts");
  }
  return false;
}

auto ParseNumber(std::string_view field, std::size_t line_number)
    -> std::uint64_t {
  std::uint64_t value = 0;
  const auto* end = field.data() + field.size();
  const auto result = std::from_chars(field.data(), end, value);
  if (result.ec != std::errc{} || result.ptr != end) {
    throw tsugumi::Failure(
        FailureKind::validation_failed,
        "Expected an unsigned integer on line " + std::to_string(line_number));
  }
  return value;
}

}  // namespace

auto run(const Card::Inputs& input) -> Card::Output {
  const auto counts_path = input.get<std::filesystem::path>("counts");
  std::ifstream source(counts_path);
  if (!source) {
    throw tsugumi::Failure(FailureKind::invalid_request, "Cannot open counts");
  }
  std::string line;
  if (!ReadLine(source, line) || line != "channel,counts") {
    throw tsugumi::Failure(FailureKind::validation_failed,
                           "Expected the header channel,counts");
  }

  const auto path = std::filesystem::current_path() / "filtered.csv";
  std::ofstream filtered(path);
  filtered << line << '\n';
  const std::vector<std::string> columns{"channel", "counts"};
  std::vector<std::vector<std::string>> preview;
  std::vector<double> preview_channels;
  std::vector<double> preview_counts;
  std::size_t line_number = 1;
  while (ReadLine(source, line)) {
    ++line_number;
    if (card::cancelled()) {
      throw tsugumi::Failure(FailureKind::cancelled, "Filtering cancelled");
    }
    const std::string_view row(line);
    const auto comma = row.find(',');
    if (comma == row.npos) {
      throw tsugumi::Failure(
          FailureKind::validation_failed,
          "Missing comma on line " + std::to_string(line_number));
    }
    const auto channel = row.substr(0, comma);
    const auto counts = row.substr(comma + 1);
    const auto channel_number = ParseNumber(channel, line_number);
    const auto count = ParseNumber(counts, line_number);
    if (count >= input.params.minimum) {
      filtered << row << '\n';
      if (preview.size() < kPreviewRows) {
        preview.push_back({std::string(channel), std::string(counts)});
        preview_channels.push_back(static_cast<double>(channel_number));
        preview_counts.push_back(static_cast<double>(count));
      }
    }
  }
  filtered.close();
  if (!filtered) {
    throw tsugumi::Failure(FailureKind::output_invalid,
                           "Cannot write filtered.csv");
  }

  card::display::send(card::display::Table{columns, preview});
  const card::display::Series series[] = {
      {"counts", "Counts by channel", preview_channels, preview_counts}};
  card::display::send(card::display::LineChart{series});
  Card::Output output;
  output.filtered = path;
  output.original = counts_path;
  return output;
}
