#include "felt/match_log.hpp"

#include "felt/equity.hpp"
#include "felt/random.hpp"

#include <algorithm>
#include <array>
#include <charconv>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <ios>
#include <limits>
#include <map>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>
#include <zlib.h>

namespace felt {
namespace {

constexpr std::uint32_t kLogSchemaVersion = 2;
constexpr std::string_view kHarnessVersion = "0.5.0-dev";
constexpr std::uint64_t kFlushInterval = 64;

std::string json_string(std::string_view text) {
  std::ostringstream output;
  output << '"';
  constexpr char hex[] = "0123456789abcdef";
  for (const unsigned char byte : text) {
    switch (byte) {
      case '"':
        output << "\\\"";
        break;
      case '\\':
        output << "\\\\";
        break;
      case '\b':
        output << "\\b";
        break;
      case '\f':
        output << "\\f";
        break;
      case '\n':
        output << "\\n";
        break;
      case '\r':
        output << "\\r";
        break;
      case '\t':
        output << "\\t";
        break;
      default:
        if (byte < 0x20U || byte >= 0x7fU) {
          output << "\\u00" << hex[byte >> 4U] << hex[byte & 0x0fU];
        } else {
          output << static_cast<char>(byte);
        }
    }
  }
  output << '"';
  return output.str();
}

std::string digest_hex(const Sha256Digest& digest) {
  std::ostringstream output;
  output << std::hex << std::setfill('0');
  for (const std::uint8_t byte : digest) {
    output << std::setw(2) << static_cast<unsigned>(byte);
  }
  return output.str();
}

std::vector<std::uint8_t> read_binary_file(const std::string& path) {
  std::ifstream input(path, std::ios::binary);
  if (!input) {
    throw std::runtime_error("could not open bot library for hashing: " + path);
  }
  input.seekg(0, std::ios::end);
  const std::streamoff size = input.tellg();
  if (size < 0 ||
      static_cast<std::uintmax_t>(size) >
          std::numeric_limits<std::size_t>::max()) {
    throw std::runtime_error("could not determine bot library size: " + path);
  }
  input.seekg(0, std::ios::beg);
  std::vector<std::uint8_t> bytes(static_cast<std::size_t>(size));
  if (!bytes.empty()) {
    input.read(reinterpret_cast<char*>(bytes.data()), size);
  }
  if (!input) {
    throw std::runtime_error("could not read bot library for hashing: " + path);
  }
  return bytes;
}

void write_card_pair(std::ostream& output,
                     const std::array<Card, 2>& cards) {
  output << '[' << static_cast<unsigned>(cards[0]) << ','
         << static_cast<unsigned>(cards[1]) << ']';
}

void write_action(std::ostream& output, const FeltAction& action) {
  output << "{\"type\":" << action.type << ",\"reserved\":"
         << action.reserved << ",\"amount_to\":" << action.amount_to << '}';
}

void write_hand(std::ostream& output,
                const MatchConfig& config,
                const MatchHand& hand) {
  output << "{\"schema_version\":" << kLogSchemaVersion
         << ",\"hand_index\":" << hand.hand_index << ",\"pair_index\":";
  if (config.duplicate) {
    output << hand.deal_index;
  } else {
    output << "null";
  }
  output << ",\"deal_index\":" << hand.deal_index
         << ",\"match_seed\":" << config.match_seed
         << ",\"deal_seed\":"
         << json_string(digest_hex(deal_seed(config.match_seed,
                                             hand.deal_index)))
         << ",\"bot_by_position\":[" << hand.bot_index_by_position[0] << ','
         << hand.bot_index_by_position[1] << "]"
         << ",\"hole_cards\":[";
  write_card_pair(output, hand.cards.hole[0]);
  output << ',';
  write_card_pair(output, hand.cards.hole[1]);
  output << "],\"board\":[";
  for (std::size_t index = 0; index < hand.cards.board.size(); ++index) {
    if (index != 0) {
      output << ',';
    }
    output << static_cast<unsigned>(hand.cards.board[index]);
  }

  output << "],\"events\":[";
  for (std::size_t index = 0; index < hand.result.events.size(); ++index) {
    if (index != 0) {
      output << ',';
    }
    const FeltActionEvent& event = hand.result.events[index];
    output << "{\"position\":" << event.position
           << ",\"street\":" << event.street << ",\"type\":"
           << event.type << ",\"amount_to\":" << event.amount_to << '}';
  }

  output << "],\"decisions\":[";
  for (std::size_t index = 0; index < hand.result.decisions.size(); ++index) {
    if (index != 0) {
      output << ',';
    }
    const DecisionRecord& decision = hand.result.decisions[index];
    output << "{\"position\":" << decision.position
           << ",\"street\":" << decision.street
           << ",\"legal_actions\":" << decision.legal_actions
           << ",\"pot\":" << decision.pot
           << ",\"my_stack\":" << decision.my_stack
           << ",\"opp_stack\":" << decision.opp_stack
           << ",\"my_street_contribution\":"
           << decision.my_street_contribution
           << ",\"opp_street_contribution\":"
           << decision.opp_street_contribution
           << ",\"to_call\":" << decision.to_call
           << ",\"min_raise_to\":" << decision.min_raise_to
           << ",\"max_raise_to\":" << decision.max_raise_to
           << ",\"decision_random\":" << decision.decision_random
           << ",\"requested\":";
    write_action(output, decision.requested);
    output << ",\"applied\":";
    write_action(output, decision.applied);
    output << ",\"violation\":"
           << static_cast<std::uint32_t>(decision.violation)
           << ",\"cpu_time_ns\":" << decision.cpu_time_ns
           << ",\"wall_time_ns\":" << decision.wall_time_ns << '}';
  }

  output << "],\"result\":{\"reason\":"
         << static_cast<std::uint32_t>(hand.result.reason)
         << ",\"ending_street\":" << hand.result.ending_street
         << ",\"folded_position\":";
  if (hand.result.folded_position == UINT32_MAX) {
    output << "null";
  } else {
    output << hand.result.folded_position;
  }
  output << ",\"committed\":[" << hand.result.committed[0] << ','
         << hand.result.committed[1] << "]"
         << ",\"raw_payout\":[" << hand.result.raw_payout[0] << ','
         << hand.result.raw_payout[1] << "]"
         << ",\"raw_net\":[" << hand.result.raw_net[0] << ','
         << hand.result.raw_net[1] << "]"
         << ",\"equity\":";
  if (!hand.result.equity_adjusted) {
    output << "null";
  } else {
    output << "{\"boards\":" << hand.result.equity_boards
           << ",\"wins\":[" << hand.result.equity_wins[0] << ','
           << hand.result.equity_wins[1] << "],\"ties\":"
           << hand.result.equity_ties << '}';
  }
  output << ",\"adjusted_payout\":[" << hand.result.adjusted_payout[0]
         << ',' << hand.result.adjusted_payout[1] << ']'
         << ",\"adjusted_net\":[" << hand.result.adjusted_net[0] << ','
         << hand.result.adjusted_net[1] << "]"
         << ",\"showdown_rank\":["
         << hand.result.showdown_rank[0] << ','
         << hand.result.showdown_rank[1] << "]}}\n";
}

}  // namespace

BotArtifact inspect_bot_artifact(const std::string& path, std::string name) {
  const std::vector<std::uint8_t> bytes = read_binary_file(path);
  return BotArtifact{std::move(name), path,
                     digest_hex(sha256(bytes.data(), bytes.size()))};
}

MatchLogWriter::MatchLogWriter(std::string output_directory,
                               MatchConfig config,
                               std::array<BotArtifact, 2> bots)
    : output_directory_(std::move(output_directory)),
      config_(config),
      bots_(std::move(bots)) {
  std::error_code error;
  std::filesystem::create_directories(output_directory_, error);
  if (error) {
    throw std::runtime_error("could not create output directory: " +
                             error.message());
  }
  const std::filesystem::path hands_path =
      std::filesystem::path(output_directory_) / "hands.jsonl";
  hands_.open(hands_path, std::ios::out | std::ios::trunc);
  if (!hands_) {
    throw std::runtime_error("could not open hands.jsonl for writing");
  }
  write_summary(nullptr);
}

MatchLogWriter::~MatchLogWriter() {
  if (hands_) {
    hands_.flush();
  }
}

void MatchLogWriter::on_hand(const MatchHand& hand) {
  if (finished_) {
    throw std::logic_error("cannot append a hand after finishing a match log");
  }
  write_hand(hands_, config_, hand);
  if (!hands_) {
    throw std::runtime_error("failed while writing hands.jsonl");
  }
  ++hands_written_;
  if ((hands_written_ % kFlushInterval) == 0U) {
    hands_.flush();
    if (!hands_) {
      throw std::runtime_error("failed while flushing hands.jsonl");
    }
  }
}

void MatchLogWriter::finish(const MatchResult& result) {
  if (finished_) {
    throw std::logic_error("match log was already finished");
  }
  if (result.hand_count != hands_written_) {
    throw std::logic_error("logged hand count did not match match result");
  }
  hands_.flush();
  if (!hands_) {
    throw std::runtime_error("failed while flushing hands.jsonl");
  }
  write_summary(&result);
  finished_ = true;
}

void MatchLogWriter::write_summary(const MatchResult* result) {
  const std::filesystem::path summary_path =
      std::filesystem::path(output_directory_) / "summary.json";
  std::ofstream output(summary_path, std::ios::out | std::ios::trunc);
  if (!output) {
    throw std::runtime_error("could not open summary.json for writing");
  }

  output << "{\n  \"schema_version\": " << kLogSchemaVersion
         << ",\n  \"harness_version\": " << json_string(kHarnessVersion)
         << ",\n  \"status\": "
         << json_string(result == nullptr ? "running" : "complete")
         << ",\n  \"config\": {\n"
         << "    \"hand_count\": " << config_.hand_count << ",\n"
         << "    \"match_seed\": " << config_.match_seed << ",\n"
         << "    \"starting_stack\": " << config_.starting_stack << ",\n"
         << "    \"small_blind\": " << config_.small_blind << ",\n"
         << "    \"big_blind\": " << config_.big_blind << ",\n"
         << "    \"decision_cap_us\": " << config_.decision_cap_us << ",\n"
         << "    \"duplicate\": " << (config_.duplicate ? "true" : "false")
         << ",\n    \"equity_adjustment\": "
         << (config_.equity_adjustment ? "true" : "false") << "\n  },\n"
         << "  \"bots\": [\n";
  for (std::size_t index = 0; index < bots_.size(); ++index) {
    output << "    {\"index\": " << index
           << ", \"name\": " << json_string(bots_[index].name)
           << ", \"path\": " << json_string(bots_[index].path)
           << ", \"sha256\": " << json_string(bots_[index].sha256) << '}';
    output << (index + 1U == bots_.size() ? "\n" : ",\n");
  }
  output << "  ],\n  \"result\": ";
  if (result == nullptr) {
    output << "null\n";
  } else {
    output << "{\n    \"hand_count\": " << result->hand_count
           << ",\n    \"raw_net_by_bot\": [" << result->raw_net_by_bot[0]
           << ", " << result->raw_net_by_bot[1] << "],\n"
           << "    \"raw_net_by_bot_and_position\": [["
           << result->raw_net_by_bot_and_position[0][0] << ", "
           << result->raw_net_by_bot_and_position[0][1] << "], ["
           << result->raw_net_by_bot_and_position[1][0] << ", "
           << result->raw_net_by_bot_and_position[1][1] << "] ],\n"
           << "    \"adjusted_net_by_bot\": ["
           << result->adjusted_net_by_bot[0] << ", "
           << result->adjusted_net_by_bot[1] << "],\n"
           << "    \"adjusted_net_by_bot_and_position\": [["
           << result->adjusted_net_by_bot_and_position[0][0] << ", "
           << result->adjusted_net_by_bot_and_position[0][1] << "], ["
           << result->adjusted_net_by_bot_and_position[1][0] << ", "
           << result->adjusted_net_by_bot_and_position[1][1] << "] ]\n  }\n";
  }
  output << "}\n";
  if (!output) {
    throw std::runtime_error("failed while writing summary.json");
  }
}

namespace {

void checked_log_add(FeltChips& total, FeltChips value) {
  constexpr FeltChips minimum = std::numeric_limits<FeltChips>::min();
  constexpr FeltChips maximum = std::numeric_limits<FeltChips>::max();
  if ((value > 0 && total > maximum - value) ||
      (value < 0 && total < minimum - value)) {
    throw std::overflow_error("logged chip total overflowed");
  }
  total += value;
}

struct JsonNumber {
  bool negative{};
  std::uint64_t magnitude{};
};

struct JsonValue {
  enum class Kind { null, boolean, number, string, array, object };

  Kind kind{Kind::null};
  bool boolean{};
  JsonNumber number;
  std::string string;
  std::vector<JsonValue> array;
  std::map<std::string, JsonValue> object;

  const JsonValue& member(std::string_view name) const {
    if (kind != Kind::object) {
      throw std::runtime_error("JSON value is not an object");
    }
    const auto found = object.find(std::string(name));
    if (found == object.end()) {
      throw std::runtime_error("missing JSON member: " + std::string(name));
    }
    return found->second;
  }

  std::uint64_t as_u64() const {
    if (kind != Kind::number || number.negative) {
      throw std::runtime_error("JSON value is not an unsigned integer");
    }
    return number.magnitude;
  }

  std::uint32_t as_u32() const {
    const std::uint64_t value = as_u64();
    if (value > std::numeric_limits<std::uint32_t>::max()) {
      throw std::runtime_error("JSON integer does not fit uint32");
    }
    return static_cast<std::uint32_t>(value);
  }

  FeltChips as_chips() const {
    if (kind != Kind::number) {
      throw std::runtime_error("JSON value is not an integer");
    }
    constexpr std::uint64_t max =
        static_cast<std::uint64_t>(std::numeric_limits<FeltChips>::max());
    if (!number.negative) {
      if (number.magnitude > max) {
        throw std::runtime_error("JSON integer does not fit FeltChips");
      }
      return static_cast<FeltChips>(number.magnitude);
    }
    if (number.magnitude > max + 1U) {
      throw std::runtime_error("JSON integer does not fit FeltChips");
    }
    if (number.magnitude == max + 1U) {
      return std::numeric_limits<FeltChips>::min();
    }
    return -static_cast<FeltChips>(number.magnitude);
  }

  bool as_bool() const {
    if (kind != Kind::boolean) {
      throw std::runtime_error("JSON value is not a boolean");
    }
    return boolean;
  }

  const std::string& as_string() const {
    if (kind != Kind::string) {
      throw std::runtime_error("JSON value is not a string");
    }
    return string;
  }

  const std::vector<JsonValue>& as_array() const {
    if (kind != Kind::array) {
      throw std::runtime_error("JSON value is not an array");
    }
    return array;
  }
};

class JsonParser {
 public:
  explicit JsonParser(std::string_view text) : text_(text) {}

  JsonValue parse() {
    JsonValue value = parse_value();
    skip_space();
    if (position_ != text_.size()) {
      fail("trailing content");
    }
    return value;
  }

 private:
  [[noreturn]] void fail(std::string_view message) const {
    throw std::runtime_error("invalid JSON at byte " +
                             std::to_string(position_) + ": " +
                             std::string(message));
  }

  void skip_space() {
    while (position_ < text_.size()) {
      const char byte = text_[position_];
      if (byte != ' ' && byte != '\t' && byte != '\n' && byte != '\r') {
        break;
      }
      ++position_;
    }
  }

  bool consume(char expected) {
    skip_space();
    if (position_ < text_.size() && text_[position_] == expected) {
      ++position_;
      return true;
    }
    return false;
  }

  void expect(char expected) {
    if (!consume(expected)) {
      fail(std::string("expected '") + expected + "'");
    }
  }

  JsonValue parse_value() {
    skip_space();
    if (position_ == text_.size()) {
      fail("expected a value");
    }
    switch (text_[position_]) {
      case 'n':
        return parse_literal("null", JsonValue::Kind::null, false);
      case 't':
        return parse_literal("true", JsonValue::Kind::boolean, true);
      case 'f':
        return parse_literal("false", JsonValue::Kind::boolean, false);
      case '"': {
        JsonValue value;
        value.kind = JsonValue::Kind::string;
        value.string = parse_string();
        return value;
      }
      case '[':
        return parse_array();
      case '{':
        return parse_object();
      default:
        if (text_[position_] == '-' ||
            (text_[position_] >= '0' && text_[position_] <= '9')) {
          return parse_number();
        }
        fail("unexpected value");
    }
  }

  JsonValue parse_literal(std::string_view literal,
                          JsonValue::Kind kind,
                          bool boolean) {
    if (text_.substr(position_, literal.size()) != literal) {
      fail("invalid literal");
    }
    position_ += literal.size();
    JsonValue value;
    value.kind = kind;
    value.boolean = boolean;
    return value;
  }

  static unsigned hex_digit(char byte) {
    if (byte >= '0' && byte <= '9') {
      return static_cast<unsigned>(byte - '0');
    }
    if (byte >= 'a' && byte <= 'f') {
      return static_cast<unsigned>(byte - 'a') + 10U;
    }
    if (byte >= 'A' && byte <= 'F') {
      return static_cast<unsigned>(byte - 'A') + 10U;
    }
    throw std::runtime_error("invalid JSON unicode escape");
  }

  void append_code_point(std::string& output, unsigned code_point) {
    if (code_point <= 0x7fU) {
      output.push_back(static_cast<char>(code_point));
    } else if (code_point <= 0x7ffU) {
      output.push_back(static_cast<char>(0xc0U | (code_point >> 6U)));
      output.push_back(static_cast<char>(0x80U | (code_point & 0x3fU)));
    } else {
      output.push_back(static_cast<char>(0xe0U | (code_point >> 12U)));
      output.push_back(
          static_cast<char>(0x80U | ((code_point >> 6U) & 0x3fU)));
      output.push_back(static_cast<char>(0x80U | (code_point & 0x3fU)));
    }
  }

  std::string parse_string() {
    expect('"');
    std::string output;
    while (position_ < text_.size()) {
      const unsigned char byte =
          static_cast<unsigned char>(text_[position_++]);
      if (byte == '"') {
        return output;
      }
      if (byte < 0x20U) {
        fail("control byte in string");
      }
      if (byte != '\\') {
        output.push_back(static_cast<char>(byte));
        continue;
      }
      if (position_ == text_.size()) {
        fail("unfinished escape");
      }
      const char escaped = text_[position_++];
      switch (escaped) {
        case '"':
        case '\\':
        case '/':
          output.push_back(escaped);
          break;
        case 'b':
          output.push_back('\b');
          break;
        case 'f':
          output.push_back('\f');
          break;
        case 'n':
          output.push_back('\n');
          break;
        case 'r':
          output.push_back('\r');
          break;
        case 't':
          output.push_back('\t');
          break;
        case 'u': {
          if (position_ + 4U > text_.size()) {
            fail("unfinished unicode escape");
          }
          unsigned code_point = 0;
          for (unsigned index = 0; index < 4U; ++index) {
            code_point =
                code_point * 16U + hex_digit(text_[position_ + index]);
          }
          position_ += 4U;
          if (code_point >= 0xd800U && code_point <= 0xdfffU) {
            fail("surrogate escapes are unsupported");
          }
          append_code_point(output, code_point);
          break;
        }
        default:
          fail("unknown string escape");
      }
    }
    fail("unterminated string");
  }

  JsonValue parse_number() {
    const std::size_t start = position_;
    bool negative = false;
    if (text_[position_] == '-') {
      negative = true;
      ++position_;
    }
    const std::size_t digits = position_;
    while (position_ < text_.size() && text_[position_] >= '0' &&
           text_[position_] <= '9') {
      ++position_;
    }
    if (digits == position_) {
      fail("number has no digits");
    }
    if (position_ < text_.size() &&
        (text_[position_] == '.' || text_[position_] == 'e' ||
         text_[position_] == 'E')) {
      fail("only integer JSON numbers are supported");
    }
    const std::size_t magnitude_start = negative ? start + 1U : start;
    std::uint64_t magnitude = 0;
    const auto [next, error] =
        std::from_chars(text_.data() + magnitude_start,
                        text_.data() + position_, magnitude);
    if (error != std::errc{} || next != text_.data() + position_) {
      fail("integer is out of range");
    }
    JsonValue value;
    value.kind = JsonValue::Kind::number;
    value.number = JsonNumber{negative, magnitude};
    return value;
  }

  JsonValue parse_array() {
    expect('[');
    JsonValue value;
    value.kind = JsonValue::Kind::array;
    if (consume(']')) {
      return value;
    }
    for (;;) {
      value.array.push_back(parse_value());
      if (consume(']')) {
        return value;
      }
      expect(',');
    }
  }

  JsonValue parse_object() {
    expect('{');
    JsonValue value;
    value.kind = JsonValue::Kind::object;
    if (consume('}')) {
      return value;
    }
    for (;;) {
      skip_space();
      if (position_ == text_.size() || text_[position_] != '"') {
        fail("object key must be a string");
      }
      std::string name = parse_string();
      expect(':');
      const auto [unused, inserted] =
          value.object.emplace(std::move(name), parse_value());
      (void)unused;
      if (!inserted) {
        fail("duplicate object key");
      }
      if (consume('}')) {
        return value;
      }
      expect(',');
    }
  }

  std::string_view text_;
  std::size_t position_{};
};

JsonValue parse_json(std::string_view text) { return JsonParser(text).parse(); }

std::string read_text_file(const std::filesystem::path& path) {
  std::ifstream input(path);
  if (!input) {
    throw std::runtime_error("could not open log file: " + path.string());
  }
  std::ostringstream content;
  content << input.rdbuf();
  if (!input.eof() && input.fail()) {
    throw std::runtime_error("could not read log file: " + path.string());
  }
  return content.str();
}

struct LoggedSummary {
  MatchConfig config;
  MatchResult result;
};

template <std::size_t Size>
std::array<FeltChips, Size> chips_array(const JsonValue& value) {
  const auto& input = value.as_array();
  if (input.size() != Size) {
    throw std::runtime_error("JSON chip array has the wrong size");
  }
  std::array<FeltChips, Size> output{};
  for (std::size_t index = 0; index < Size; ++index) {
    output[index] = input[index].as_chips();
  }
  return output;
}

LoggedSummary load_summary(const std::string& output_directory) {
  const std::filesystem::path path =
      std::filesystem::path(output_directory) / "summary.json";
  const JsonValue root = parse_json(read_text_file(path));
  if (root.member("schema_version").as_u32() != kLogSchemaVersion ||
      root.member("status").as_string() != "complete") {
    throw std::runtime_error("match summary is incomplete or unsupported");
  }

  const JsonValue& config_json = root.member("config");
  LoggedSummary summary;
  summary.config.hand_count = config_json.member("hand_count").as_u64();
  summary.config.match_seed = config_json.member("match_seed").as_u64();
  summary.config.starting_stack =
      config_json.member("starting_stack").as_chips();
  summary.config.small_blind = config_json.member("small_blind").as_chips();
  summary.config.big_blind = config_json.member("big_blind").as_chips();
  summary.config.decision_cap_us =
      config_json.member("decision_cap_us").as_u64();
  summary.config.duplicate = config_json.member("duplicate").as_bool();
  summary.config.equity_adjustment =
      config_json.member("equity_adjustment").as_bool();
  validate_match_config(summary.config);

  const JsonValue& result_json = root.member("result");
  summary.result.hand_count = result_json.member("hand_count").as_u64();
  summary.result.raw_net_by_bot =
      chips_array<2>(result_json.member("raw_net_by_bot"));
  const auto& positions =
      result_json.member("raw_net_by_bot_and_position").as_array();
  if (positions.size() != 2U) {
    throw std::runtime_error("summary position results have the wrong size");
  }
  for (std::size_t bot = 0; bot < 2; ++bot) {
    summary.result.raw_net_by_bot_and_position[bot] =
        chips_array<2>(positions[bot]);
  }
  summary.result.adjusted_net_by_bot =
      chips_array<2>(result_json.member("adjusted_net_by_bot"));
  const auto& adjusted_positions =
      result_json.member("adjusted_net_by_bot_and_position").as_array();
  if (adjusted_positions.size() != 2U) {
    throw std::runtime_error(
        "summary adjusted position results have the wrong size");
  }
  for (std::size_t bot = 0; bot < 2; ++bot) {
    summary.result.adjusted_net_by_bot_and_position[bot] =
        chips_array<2>(adjusted_positions[bot]);
  }
  if (summary.result.hand_count != summary.config.hand_count) {
    throw std::runtime_error("summary hand counts do not agree");
  }
  return summary;
}

FeltAction parse_action(const JsonValue& json) {
  return FeltAction{json.member("type").as_u32(),
                    json.member("reserved").as_u32(),
                    json.member("amount_to").as_chips()};
}

struct LoggedHand {
  std::uint64_t hand_index{};
  std::uint64_t deal_index{};
  std::array<std::uint32_t, 2> bot_index_by_position{};
  HandCards cards;
  std::vector<DecisionRecord> decisions;
  HandResult result;
};

LoggedHand parse_logged_hand(std::string_view line,
                             const MatchConfig& config) {
  const JsonValue root = parse_json(line);
  if (root.member("schema_version").as_u32() != kLogSchemaVersion) {
    throw std::runtime_error("unsupported hand-log schema version");
  }
  LoggedHand hand;
  hand.hand_index = root.member("hand_index").as_u64();
  hand.deal_index = root.member("deal_index").as_u64();
  if (root.member("match_seed").as_u64() != config.match_seed) {
    throw std::runtime_error("hand match seed disagrees with summary");
  }
  const std::string expected_deal_seed =
      digest_hex(deal_seed(config.match_seed, hand.deal_index));
  if (root.member("deal_seed").as_string() != expected_deal_seed) {
    throw std::runtime_error("logged deal seed is inconsistent");
  }

  const auto& bot_positions = root.member("bot_by_position").as_array();
  if (bot_positions.size() != 2U) {
    throw std::runtime_error("bot position array has the wrong size");
  }
  for (std::size_t position = 0; position < 2; ++position) {
    hand.bot_index_by_position[position] = bot_positions[position].as_u32();
    if (hand.bot_index_by_position[position] > 1U) {
      throw std::runtime_error("invalid bot index in hand log");
    }
  }
  if (hand.bot_index_by_position[0] == hand.bot_index_by_position[1]) {
    throw std::runtime_error("bot position array is not a permutation");
  }

  const auto& holes = root.member("hole_cards").as_array();
  if (holes.size() != 2U) {
    throw std::runtime_error("hole-card array has the wrong size");
  }
  for (std::size_t position = 0; position < 2; ++position) {
    const auto& cards = holes[position].as_array();
    if (cards.size() != 2U) {
      throw std::runtime_error("hole-card pair has the wrong size");
    }
    for (std::size_t card = 0; card < 2; ++card) {
      const std::uint32_t value = cards[card].as_u32();
      if (value > std::numeric_limits<Card>::max()) {
        throw std::runtime_error("logged card does not fit card storage");
      }
      hand.cards.hole[position][card] = static_cast<Card>(value);
    }
  }
  const auto& board = root.member("board").as_array();
  if (board.size() != 5U) {
    throw std::runtime_error("board array has the wrong size");
  }
  for (std::size_t index = 0; index < board.size(); ++index) {
    const std::uint32_t value = board[index].as_u32();
    if (value > std::numeric_limits<Card>::max()) {
      throw std::runtime_error("logged card does not fit card storage");
    }
    hand.cards.board[index] = static_cast<Card>(value);
  }

  for (const JsonValue& event_json : root.member("events").as_array()) {
    hand.result.events.push_back(FeltActionEvent{
        event_json.member("position").as_u32(),
        event_json.member("street").as_u32(),
        event_json.member("type").as_u32(), 0U,
        event_json.member("amount_to").as_chips()});
  }

  for (const JsonValue& decision_json :
       root.member("decisions").as_array()) {
    DecisionRecord decision;
    decision.position = decision_json.member("position").as_u32();
    decision.street = decision_json.member("street").as_u32();
    decision.legal_actions =
        decision_json.member("legal_actions").as_u32();
    decision.pot = decision_json.member("pot").as_chips();
    decision.my_stack = decision_json.member("my_stack").as_chips();
    decision.opp_stack = decision_json.member("opp_stack").as_chips();
    decision.my_street_contribution =
        decision_json.member("my_street_contribution").as_chips();
    decision.opp_street_contribution =
        decision_json.member("opp_street_contribution").as_chips();
    decision.to_call = decision_json.member("to_call").as_chips();
    decision.min_raise_to =
        decision_json.member("min_raise_to").as_chips();
    decision.max_raise_to =
        decision_json.member("max_raise_to").as_chips();
    decision.decision_random =
        decision_json.member("decision_random").as_u64();
    decision.requested = parse_action(decision_json.member("requested"));
    decision.applied = parse_action(decision_json.member("applied"));
    decision.violation = static_cast<ActionViolation>(
        decision_json.member("violation").as_u32());
    decision.cpu_time_ns = decision_json.member("cpu_time_ns").as_u64();
    decision.wall_time_ns = decision_json.member("wall_time_ns").as_u64();
    hand.decisions.push_back(decision);
  }

  const JsonValue& result = root.member("result");
  hand.result.reason =
      static_cast<HandEndReason>(result.member("reason").as_u32());
  hand.result.ending_street = result.member("ending_street").as_u32();
  const JsonValue& folded = result.member("folded_position");
  hand.result.folded_position =
      folded.kind == JsonValue::Kind::null ? UINT32_MAX : folded.as_u32();
  hand.result.committed = chips_array<2>(result.member("committed"));
  hand.result.raw_payout = chips_array<2>(result.member("raw_payout"));
  hand.result.raw_net = chips_array<2>(result.member("raw_net"));
  const JsonValue& equity = result.member("equity");
  if (equity.kind != JsonValue::Kind::null) {
    hand.result.equity_adjusted = true;
    hand.result.equity_boards = equity.member("boards").as_u64();
    const auto& wins = equity.member("wins").as_array();
    if (wins.size() != 2U) {
      throw std::runtime_error("equity win array has the wrong size");
    }
    hand.result.equity_wins = {wins[0].as_u64(), wins[1].as_u64()};
    hand.result.equity_ties = equity.member("ties").as_u64();
  }
  hand.result.adjusted_payout =
      chips_array<2>(result.member("adjusted_payout"));
  hand.result.adjusted_net =
      chips_array<2>(result.member("adjusted_net"));
  const auto& ranks = result.member("showdown_rank").as_array();
  if (ranks.size() != 2U) {
    throw std::runtime_error("showdown rank array has the wrong size");
  }
  for (std::size_t position = 0; position < 2; ++position) {
    const std::uint32_t rank = ranks[position].as_u32();
    if (rank > std::numeric_limits<std::uint16_t>::max()) {
      throw std::runtime_error("showdown rank is out of range");
    }
    hand.result.showdown_rank[position] = static_cast<std::uint16_t>(rank);
  }
  return hand;
}

bool same_action(const FeltAction& left, const FeltAction& right) {
  return left.type == right.type && left.reserved == right.reserved &&
         left.amount_to == right.amount_to;
}

bool same_event(const FeltActionEvent& left, const FeltActionEvent& right) {
  return left.position == right.position && left.street == right.street &&
         left.type == right.type && left.amount_to == right.amount_to;
}

bool same_decision_state(const DecisionRecord& left,
                         const DecisionRecord& right) {
  return left.position == right.position && left.street == right.street &&
         left.legal_actions == right.legal_actions && left.pot == right.pot &&
         left.my_stack == right.my_stack &&
         left.opp_stack == right.opp_stack &&
         left.my_street_contribution == right.my_street_contribution &&
         left.opp_street_contribution == right.opp_street_contribution &&
         left.to_call == right.to_call &&
         left.min_raise_to == right.min_raise_to &&
         left.max_raise_to == right.max_raise_to &&
         left.decision_random == right.decision_random;
}

bool same_terminal_result(const HandResult& left, const HandResult& right) {
  return left.reason == right.reason &&
         left.ending_street == right.ending_street &&
         left.folded_position == right.folded_position &&
         left.committed == right.committed &&
         left.raw_payout == right.raw_payout && left.raw_net == right.raw_net &&
         left.equity_adjusted == right.equity_adjusted &&
         left.equity_boards == right.equity_boards &&
         left.equity_wins == right.equity_wins &&
         left.equity_ties == right.equity_ties &&
         left.adjusted_payout == right.adjusted_payout &&
         left.adjusted_net == right.adjusted_net &&
         left.showdown_rank == right.showdown_rank;
}

class ReplayBot final : public BotRunner {
 public:
  explicit ReplayBot(std::vector<FeltAction> actions)
      : actions_(std::move(actions)) {}

  std::string_view name() const noexcept override { return "replay"; }

  FeltAction act(const FeltGameState&) override {
    if (next_ == actions_.size()) {
      throw std::runtime_error("logged action sequence ended too early");
    }
    return actions_[next_++];
  }

  bool complete() const noexcept { return next_ == actions_.size(); }

 private:
  std::vector<FeltAction> actions_;
  std::size_t next_{};
};

HandResult replay_hand(const LoggedHand& logged,
                       const MatchConfig& config,
                       ExactEquityCalculator& equity_calculator) {
  std::array<std::vector<FeltAction>, 2> actions;
  for (const DecisionRecord& decision : logged.decisions) {
    if (decision.position > 1U) {
      throw std::runtime_error("invalid decision position in hand log");
    }
    actions[decision.position].push_back(decision.applied);
  }
  ReplayBot button(std::move(actions[FELT_POSITION_BUTTON]));
  ReplayBot big_blind(std::move(actions[FELT_POSITION_BIG_BLIND]));
  std::array<BotRunner*, 2> bots{&button, &big_blind};

  HandConfig hand_config;
  hand_config.starting_stack = config.starting_stack;
  hand_config.small_blind = config.small_blind;
  hand_config.big_blind = config.big_blind;
  hand_config.decision_cap_us = config.decision_cap_us;
  hand_config.match_seed = config.match_seed;
  hand_config.randomness_index = logged.deal_index;
  HandResult replayed = play_hand(hand_config, logged.cards, bots);
  if (config.equity_adjustment) {
    apply_equity_adjustment(replayed, logged.cards, config.starting_stack,
                            equity_calculator);
  }
  if (!button.complete() || !big_blind.complete()) {
    throw std::runtime_error("logged action sequence had unused actions");
  }
  return replayed;
}

void verify_replayed_hand(const LoggedHand& logged,
                          const HandResult& replayed) {
  if (logged.result.events.size() != replayed.events.size()) {
    throw std::runtime_error("replay event count differed");
  }
  for (std::size_t index = 0; index < replayed.events.size(); ++index) {
    if (!same_event(logged.result.events[index], replayed.events[index])) {
      throw std::runtime_error("replay event differed at index " +
                               std::to_string(index));
    }
  }
  if (logged.decisions.size() != replayed.decisions.size()) {
    throw std::runtime_error("replay decision count differed");
  }
  for (std::size_t index = 0; index < replayed.decisions.size(); ++index) {
    if (!same_decision_state(logged.decisions[index],
                             replayed.decisions[index]) ||
        !same_action(logged.decisions[index].applied,
                     replayed.decisions[index].applied)) {
      throw std::runtime_error("replay decision differed at index " +
                               std::to_string(index));
    }
  }
  if (!same_terminal_result(logged.result, replayed)) {
    throw std::runtime_error("replay terminal state differed");
  }
}

class HandLogReader {
 public:
  explicit HandLogReader(const std::string& output_directory) {
    const std::filesystem::path directory(output_directory);
    const std::filesystem::path plain_path = directory / "hands.jsonl";
    const std::filesystem::path gzip_path = directory / "hands.jsonl.gz";
    if (std::filesystem::is_regular_file(plain_path)) {
      plain_.open(plain_path);
      if (!plain_) {
        throw std::runtime_error("could not open log file: " +
                                 plain_path.string());
      }
      return;
    }
    if (std::filesystem::is_regular_file(gzip_path)) {
      gzip_ = gzopen(gzip_path.string().c_str(), "rb");
      if (gzip_ == nullptr) {
        throw std::runtime_error("could not open gzip log file: " +
                                 gzip_path.string());
      }
      gzip_path_ = gzip_path.string();
      return;
    }
    throw std::runtime_error("could not find hands.jsonl or hands.jsonl.gz in: " +
                             output_directory);
  }

  ~HandLogReader() {
    if (gzip_ != nullptr) {
      (void)gzclose(gzip_);
    }
  }

  HandLogReader(const HandLogReader&) = delete;
  HandLogReader& operator=(const HandLogReader&) = delete;

  bool read_line(std::string& line) {
    line.clear();
    if (gzip_ == nullptr) {
      if (std::getline(plain_, line)) {
        return true;
      }
      if (!plain_.eof()) {
        throw std::runtime_error("failed while reading hands.jsonl");
      }
      return false;
    }

    std::array<char, 65'536> buffer{};
    for (;;) {
      char* const chunk =
          gzgets(gzip_, buffer.data(), static_cast<int>(buffer.size()));
      if (chunk == nullptr) {
        if (gzeof(gzip_) != 0) {
          return !line.empty();
        }
        int error_code = Z_OK;
        const char* const message = gzerror(gzip_, &error_code);
        throw std::runtime_error("failed while reading " + gzip_path_ + ": " +
                                 (message == nullptr ? "gzip error" : message));
      }
      const std::size_t length = std::strlen(buffer.data());
      if (length != 0U && buffer[length - 1U] == '\n') {
        line.append(buffer.data(), length - 1U);
        if (!line.empty() && line.back() == '\r') {
          line.pop_back();
        }
        return true;
      }
      line.append(buffer.data(), length);
    }
  }

 private:
  std::ifstream plain_;
  gzFile gzip_{nullptr};
  std::string gzip_path_;
};

}  // namespace

ReplayReport replay_match_log(const std::string& output_directory) {
  const LoggedSummary summary = load_summary(output_directory);
  HandLogReader hands(output_directory);
  ReplayReport report;
  MatchResult reconstructed;
  ExactEquityCalculator equity_calculator;
  HandCards previous_cards;
  std::string line;
  while (hands.read_line(line)) {
    if (line.empty()) {
      throw std::runtime_error("empty line in hands.jsonl");
    }
    const LoggedHand logged = parse_logged_hand(line, summary.config);
    if (logged.hand_index != report.hands_verified) {
      throw std::runtime_error("hand indices are not contiguous");
    }
    const std::uint64_t expected_deal_index =
        summary.config.duplicate ? logged.hand_index / 2U : logged.hand_index;
    const std::array<std::uint32_t, 2> expected_bots{
        static_cast<std::uint32_t>(logged.hand_index % 2U),
        static_cast<std::uint32_t>(1U - (logged.hand_index % 2U))};
    if (logged.deal_index != expected_deal_index ||
        logged.bot_index_by_position != expected_bots) {
      throw std::runtime_error("logged deal or seat schedule is inconsistent");
    }
    if (summary.config.duplicate && (logged.hand_index % 2U) == 1U &&
        (logged.cards.hole != previous_cards.hole ||
         logged.cards.board != previous_cards.board)) {
      throw std::runtime_error("duplicate pair cards differ in the log");
    }
    verify_replayed_hand(
        logged, replay_hand(logged, summary.config, equity_calculator));
    for (std::size_t position = 0; position < 2; ++position) {
      const std::size_t bot = logged.bot_index_by_position[position];
      checked_log_add(reconstructed.raw_net_by_bot[bot],
                      logged.result.raw_net[position]);
      checked_log_add(reconstructed.adjusted_net_by_bot[bot],
                      logged.result.adjusted_net[position]);
      checked_log_add(
          reconstructed.raw_net_by_bot_and_position[bot][position],
                      logged.result.raw_net[position]);
      checked_log_add(
          reconstructed.adjusted_net_by_bot_and_position[bot][position],
          logged.result.adjusted_net[position]);
    }
    reconstructed.hand_count++;
    previous_cards = logged.cards;
    ++report.hands_verified;
  }
  if (report.hands_verified != summary.result.hand_count) {
    throw std::runtime_error("hands.jsonl count disagrees with summary");
  }
  if (reconstructed.hand_count != summary.result.hand_count ||
      reconstructed.raw_net_by_bot != summary.result.raw_net_by_bot ||
      reconstructed.adjusted_net_by_bot !=
          summary.result.adjusted_net_by_bot ||
      reconstructed.raw_net_by_bot_and_position !=
          summary.result.raw_net_by_bot_and_position ||
      reconstructed.adjusted_net_by_bot_and_position !=
          summary.result.adjusted_net_by_bot_and_position) {
    throw std::runtime_error("hand-log totals disagree with summary");
  }
  return report;
}

RerunReport rerun_match_log(const std::string& output_directory,
                            BotRunner& bot_a,
                            BotRunner& bot_b) {
  const LoggedSummary summary = load_summary(output_directory);

  class Comparator final : public MatchObserver {
   public:
    Comparator(const std::string& output_directory, MatchConfig config)
        : input_(output_directory), config_(config) {}

    void on_hand(const MatchHand& fresh) override {
      std::string line;
      if (!input_.read_line(line)) {
        throw std::runtime_error("rerun produced more hands than the log");
      }
      const LoggedHand logged = parse_logged_hand(line, config_);
      if (fresh.hand_index != logged.hand_index ||
          fresh.deal_index != logged.deal_index ||
          fresh.bot_index_by_position != logged.bot_index_by_position ||
          fresh.cards.hole != logged.cards.hole ||
          fresh.cards.board != logged.cards.board) {
        throw std::runtime_error("rerun deal metadata differed from log");
      }

      bool hand_different =
          !same_terminal_result(fresh.result, logged.result) ||
          fresh.result.decisions.size() != logged.decisions.size();
      const std::size_t shared =
          std::min(fresh.result.decisions.size(), logged.decisions.size());
      for (std::size_t index = 0; index < shared; ++index) {
        if (!same_action(fresh.result.decisions[index].requested,
                         logged.decisions[index].requested) ||
            !same_action(fresh.result.decisions[index].applied,
                         logged.decisions[index].applied)) {
          ++report_.decisions_different;
          hand_different = true;
        }
      }
      report_.decisions_different +=
          static_cast<std::uint64_t>(fresh.result.decisions.size() - shared);
      report_.decisions_different +=
          static_cast<std::uint64_t>(logged.decisions.size() - shared);
      if (hand_different) {
        ++report_.hands_different;
      }
      ++report_.hands_compared;
    }

    RerunReport finish() {
      std::string extra;
      if (input_.read_line(extra)) {
        throw std::runtime_error("rerun produced fewer hands than the log");
      }
      return report_;
    }

   private:
    HandLogReader input_;
    MatchConfig config_;
    RerunReport report_;
  };

  Comparator comparator(output_directory, summary.config);
  (void)play_match(summary.config, bot_a, bot_b, &comparator);
  RerunReport report = comparator.finish();
  if (report.hands_compared != summary.result.hand_count) {
    throw std::runtime_error("rerun hand count disagrees with summary");
  }
  return report;
}

}  // namespace felt
