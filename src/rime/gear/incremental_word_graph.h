//
// Copyright RIME Developers
// Distributed under the BSD License
//
#ifndef RIME_INCREMENTAL_WORD_GRAPH_H_
#define RIME_INCREMENTAL_WORD_GRAPH_H_

#include <rime/algo/syllabifier.h>
#include <rime/dict/dictionary.h>
#include <rime/dict/user_dictionary.h>

namespace rime {

struct WordGraphState;

// A version's input spans and weighted matches. Unchanged rows are shared
// with other versions.
class RIME_DLL WordGraph {
 public:
  struct Row {
    map<int, DictEntryList> entries;
    set<size_t> accessed_positions;
  };
  using Vertices = map<int, an<const Row>>;

  WordGraph();
  const Vertices& edges() const;
  an<const Row> FindEdges(int start) const;
  void AddEdges(int start, an<const Row> row);

  bool GetPhrases(an<DictEntryCollector>* phrases,
                  an<UserDictEntryCollector>* user_phrases) const;
  void SetPhrases(const an<DictEntryCollector>& phrases,
                  const an<UserDictEntryCollector>& user_phrases,
                  set<size_t> accessed_positions);

 private:
  friend class IncrementalWordGraph;
  explicit WordGraph(an<WordGraphState> state);
  an<WordGraphState> state_;
};

class RIME_DLL IncrementalWordGraph {
 public:
  IncrementalWordGraph();
  ~IncrementalWordGraph();

  // Input prefixes form a trie. Checking out an earlier input reverts to its
  // version; editing after that creates or revisits another branch.
  WordGraph Advance(const string& input,
                    const SyllableGraph& syllables,
                    Dictionary* dictionary,
                    UserDictionary* user_dictionary,
                    const hash_set<string>& blacklist,
                    int max_homophones,
                    bool predict_word);
  void Clear();
  // Limits retained history; translations holding a view keep it alive.
  void Prune(size_t max_versions = 32, size_t max_entries = 16384);

  size_t version_count() const { return recent_.size(); }

 private:
  struct Node;
  the<Node> root_;
  Node* current_ = nullptr;
  list<Node*> recent_;
  Dictionary* dictionary_ = nullptr;
  UserDictionary* user_dictionary_ = nullptr;
  uint64_t user_revision_ = 0;
  hash_set<string> blacklist_;
  int max_homophones_ = 0;
};

}  // namespace rime

#endif  // RIME_INCREMENTAL_WORD_GRAPH_H_
