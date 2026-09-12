//
// Copyright RIME Developers
// Distributed under the BSD License
//
#include <algorithm>
#include <tuple>
#include <rime/gear/incremental_word_graph.h>

namespace rime {

struct WordGraphState {
  EdgeMap syllables;
  size_t interpreted_length = 0;
  WordGraph::Vertices rows;
  bool predict_word = false;
  bool has_phrases = false;
  an<DictEntryCollector> phrases;
  an<UserDictEntryCollector> user_phrases;
  set<size_t> accessed_positions;
};

struct IncrementalWordGraph::Node {
  Node* parent = nullptr;
  char key = 0;
  map<char, the<Node>> children;
  an<WordGraphState> state;
};

namespace {

an<DictEntryCollector> Clone(const an<DictEntryCollector>& source) {
  if (!source)
    return nullptr;
  auto copy = New<DictEntryCollector>();
  for (const auto& entry : *source)
    copy->emplace(entry.first, entry.second.Clone());
  return copy;
}

an<UserDictEntryCollector> Clone(const an<UserDictEntryCollector>& source) {
  if (!source)
    return nullptr;
  auto copy = New<UserDictEntryCollector>();
  for (const auto& entry : *source) {
    auto iter = entry.second;
    auto& target = (*copy)[entry.first];
    while (!iter.exhausted()) {
      target.Add(New<DictEntry>(*iter.Peek()));
      iter.Next();
    }
  }
  return copy;
}

bool SameEdges(const EndVertexMap& a, const EndVertexMap& b) {
  return std::equal(
      a.begin(), a.end(), b.begin(), b.end(), [](const auto& x, const auto& y) {
        return x.first == y.first &&
               std::equal(x.second.begin(), x.second.end(), y.second.begin(),
                          y.second.end(), [](const auto& p, const auto& q) {
                            const auto& u = p.second;
                            const auto& v = q.second;
                            return p.first == q.first &&
                                   std::tie(u.type, u.end_pos, u.credibility,
                                            u.tips, u.is_correction,
                                            u.ambiguous_source_positions) ==
                                       std::tie(v.type, v.end_pos,
                                                v.credibility, v.tips,
                                                v.is_correction,
                                                v.ambiguous_source_positions);
                          });
      });
}

bool Unchanged(const WordGraphState& previous,
               const SyllableGraph& current,
               const set<size_t>& positions) {
  for (size_t pos : positions) {
    if ((pos >= previous.interpreted_length) !=
        (pos >= current.interpreted_length))
      return false;
    auto a = previous.syllables.find(pos);
    auto b = current.edges.find(pos);
    if (a == previous.syllables.end() || b == current.edges.end()) {
      if ((a == previous.syllables.end()) != (b == current.edges.end()))
        return false;
    } else if (!SameEdges(a->second, b->second)) {
      return false;
    }
  }
  return true;
}

size_t EntryCount(const WordGraphState& state) {
  size_t count = 0;
  for (const auto& start : state.syllables)
    for (const auto& end : start.second)
      count += end.second.size();
  for (const auto& start : state.rows)
    for (const auto& end : start.second->entries)
      count += end.second.size();
  if (state.phrases)
    for (const auto& entry : *state.phrases)
      count += entry.second.entry_count();
  if (state.user_phrases)
    for (const auto& entry : *state.user_phrases)
      count += entry.second.cache_size();
  return count;
}

}  // namespace

WordGraph::WordGraph() : state_(New<WordGraphState>()) {}

WordGraph::WordGraph(an<WordGraphState> state) : state_(std::move(state)) {}

const WordGraph::Vertices& WordGraph::edges() const {
  return state_->rows;
}

an<const WordGraph::Row> WordGraph::FindEdges(int start) const {
  auto found = state_->rows.find(start);
  return found == state_->rows.end() ? nullptr : found->second;
}

void WordGraph::AddEdges(int start, an<const Row> row) {
  state_->rows.emplace(start, std::move(row));
}

bool WordGraph::GetPhrases(an<DictEntryCollector>* phrases,
                           an<UserDictEntryCollector>* user_phrases) const {
  if (!state_->has_phrases)
    return false;
  *phrases = Clone(state_->phrases);
  *user_phrases = Clone(state_->user_phrases);
  return true;
}

void WordGraph::SetPhrases(const an<DictEntryCollector>& phrases,
                           const an<UserDictEntryCollector>& user_phrases,
                           set<size_t> accessed_positions) {
  state_->phrases = Clone(phrases);
  state_->user_phrases = Clone(user_phrases);
  state_->accessed_positions = std::move(accessed_positions);
  state_->has_phrases = true;
}

IncrementalWordGraph::IncrementalWordGraph() {
  Clear();
}

IncrementalWordGraph::~IncrementalWordGraph() = default;

void IncrementalWordGraph::Clear() {
  recent_.clear();
  current_ = nullptr;
  root_ = make_unique<Node>();
}

WordGraph IncrementalWordGraph::Advance(const string& input,
                                        const SyllableGraph& syllables,
                                        Dictionary* dictionary,
                                        UserDictionary* user_dictionary,
                                        const hash_set<string>& blacklist,
                                        int max_homophones,
                                        bool predict_word) {
  uint64_t revision = user_dictionary ? user_dictionary->revision() : 0;
  if (dictionary_ != dictionary || user_dictionary_ != user_dictionary ||
      user_revision_ != revision || (user_dictionary && !revision) ||
      blacklist_ != blacklist || max_homophones_ != max_homophones) {
    Clear();
  }
  dictionary_ = dictionary;
  user_dictionary_ = user_dictionary;
  user_revision_ = revision;
  blacklist_ = blacklist;
  max_homophones_ = max_homophones;

  auto* node = root_.get();
  for (char key : input) {
    auto& child = node->children[key];
    if (!child) {
      child = make_unique<Node>();
      child->parent = node;
      child->key = key;
    }
    node = child.get();
  }
  auto previous = node->state ? node->state
                  : current_  ? current_->state
                              : nullptr;
  auto state = New<WordGraphState>();
  state->syllables = syllables.edges;
  state->interpreted_length = syllables.interpreted_length;
  state->predict_word = predict_word;
  if (previous) {
    for (const auto& row : previous->rows) {
      if (syllables.edges.count(row.first) &&
          Unchanged(*previous, syllables, row.second->accessed_positions)) {
        state->rows.insert(row);
      }
    }
    if (previous->has_phrases && previous->predict_word == predict_word &&
        Unchanged(*previous, syllables, previous->accessed_positions)) {
      state->has_phrases = true;
      state->phrases = previous->phrases;
      state->user_phrases = previous->user_phrases;
      state->accessed_positions = previous->accessed_positions;
    }
  }
  node->state = state;
  current_ = node;
  recent_.remove(node);
  recent_.push_back(node);
  return WordGraph(std::move(state));
}

void IncrementalWordGraph::Prune(size_t max_versions, size_t max_entries) {
  size_t entries = 0;
  for (auto* node : recent_)
    entries += EntryCount(*node->state);
  // Keep the current version even if it alone exceeds the history budget.
  while (recent_.size() > 1 &&
         (recent_.size() > max_versions || entries > max_entries)) {
    auto* node = recent_.front();
    recent_.pop_front();
    entries -= EntryCount(*node->state);
    node->state.reset();
    while (node != root_.get() && !node->state && node->children.empty()) {
      auto* parent = node->parent;
      parent->children.erase(node->key);
      node = parent;
    }
  }
}

}  // namespace rime
