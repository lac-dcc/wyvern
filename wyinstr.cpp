#include <cstdio>
#include <cstdlib>
#include <cstring>

#include <stack>
#include "boost/unordered_map.hpp"
#include "boost/thread/recursive_mutex.hpp"

using callsite_id = std::pair<char *, int64_t>;

class CallStack {
  int64_t first;
  callsite_id arr[800000];

  public:
  CallStack() { first = -1; }

  void push(callsite_id entry) {
    if (first >= 800000) {
      throw std::runtime_error("Stack overflow!");
    }
    arr[++first] = entry;
  }

  void pop() {
    if (first < 0) {
      throw std::runtime_error("Popping from empty stack!");
    }
    first--;
  }

  callsite_id top() {
    if (first < 0) {
      throw std::runtime_error("Top of empty stack!");
    }
    return arr[first];
  }

  bool empty() {
    return (first < 0);
  }
};

size_t hash_c_string(const char* p, size_t s) {
    size_t result = 0;
    const size_t prime = 31;
    for (size_t i = 0; i < s; ++i) {
        result = p[i] + (result * prime);
    }
    return result;
}

struct pair_hash {
  std::size_t operator()(const std::pair<char*, int64_t> &p) const {
    auto h1 = hash_c_string(p.first, strlen(p.first));
    auto h2 = std::hash<int64_t>{}(p.second);

    return h1 ^ h2;
  }
};

struct prof_report {
  prof_report(int8_t num_args)
      : _num_calls(1), _num_args(num_args) {
    _unique_arg_evals = (int64_t*) calloc(num_args, sizeof(int64_t));
    _arg_evals = (int64_t*) calloc(num_args, sizeof(int64_t));
  }

  ~prof_report() {
    free(_unique_arg_evals);
    free(_arg_evals);
  }

  int64_t _num_calls;
  int8_t _num_args;
  int64_t *_unique_arg_evals;
  int64_t *_arg_evals;
};

static bool initialized = false;

static boost::unordered_map<callsite_id, std::unique_ptr<struct prof_report>,
                          pair_hash>
    profile_info;
static CallStack call_stack;
boost::recursive_mutex wyinstr_mutex;

extern "C" void __attribute__((noinline))
_wyinstr_init_call(char *fun_name, int64_t callinstr_id, int8_t num_args) {
  boost::recursive_mutex::scoped_lock lock(wyinstr_mutex);
  if (initialized == false && strcmp(fun_name, "main") == 0) {
    initialized = true;
  }
#ifdef DEBUG
  fprintf(stderr, "Adding call from function %s with %d arguments!\n", fun_name,
          num_args);
#endif

  callsite_id id = std::make_pair(fun_name, callinstr_id);
  // first time callsite is reached, instantiate new profile report
  if (!profile_info.count(id)) {
#ifdef DEBUG
    fprintf(stderr,
            "First call on this callsite!. Inserting profile report... Profile "
            "map size: %li\n",
            profile_info.size());
#endif
    profile_info.emplace(id, std::make_unique<struct prof_report>(num_args));
  }

  struct prof_report *report = profile_info[id].get();
  report->_num_calls += 1;

  call_stack.push(id);

#ifdef DEBUG
  fprintf(stderr, "New top of stack: <%s, %li>\n", fun_name, callinstr_id);
#endif
}

extern "C" __attribute__((noinline)) void _wyinstr_mark_eval(int8_t arg_index,
                                                             int64_t *bits) {
  boost::recursive_mutex::scoped_lock lock(wyinstr_mutex);
  if (!initialized) {
    return;
  }
#ifdef DEBUG
  fprintf(stderr, "Logging eval of arg: %d\n", arg_index);
#endif

  if (call_stack.empty()) {
    return;
  }

  callsite_id id = call_stack.top();
  struct prof_report *report = profile_info[id].get();

  // first arg eval in this call, increment unique counter
  if ((*bits & (1 << arg_index)) == 0) {
    *bits = *bits | (1 << arg_index);
    report->_unique_arg_evals[arg_index] += 1;
  }

  // increment total eval counter
  report->_arg_evals[arg_index] += 1;

#ifdef DEBUG
  fprintf(stderr, "Total arg evals: %li\n", report->_arg_evals[arg_index]);
#endif
}

extern "C" __attribute__((noinline)) int64_t _wyinstr_initbits() {
  return static_cast<int64_t>(0);
}

extern "C" __attribute__((noinline)) void _wyinstr_end_call() {
  boost::recursive_mutex::scoped_lock lock(wyinstr_mutex);
#ifdef DEBUG
  fprintf(stderr, "Ending call from callsite <%s, %li>.\n",
          call_stack.top().first, call_stack.top().second);
#endif
  call_stack.pop();
#ifdef DEBUG
  if (!call_stack.empty()) {
    fprintf(stderr, "New top: <%s, %li>.\n", call_stack.top().first,
            call_stack.top().second);
  }
#endif
}

extern "C" __attribute__((noinline)) void _wyinstr_dump() {
  boost::recursive_mutex::scoped_lock lock(wyinstr_mutex);
  FILE *outfile = fopen("wyinstr_output.csv", "w");
  fprintf(outfile, "fun_name,call_id,num_args,unique_evals,total_evals\n");
  for (auto &[key, value] : profile_info) {
    fprintf(outfile, "%s,%li,%d,", key.first, key.second,
            value->_num_args);
    for (int8_t i = 0; i < value->_num_args; ++i) {
      fprintf(outfile, "%li,", value->_unique_arg_evals[i]);
    }
    for (int8_t i = 0; i < value->_num_args; ++i) {
      fprintf(outfile, "%li,", value->_arg_evals[i]);
    }
    fprintf(outfile, "\n");
  }
  fclose(outfile);
}
