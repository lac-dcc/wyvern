#include <cstdio>
#include <cstdlib>
#include <cstring>

#include <memory>
#include <mutex>
#include <stack>
#include <map>

using callsite_id = std::pair<const char *, int64_t>;

size_t hash_c_string(const char *p, size_t s) {
  size_t result = 0;
  const size_t prime = 31;
  for (size_t i = 0; i < s; ++i) {
    result = p[i] + (result * prime);
  }
  return result;
}

struct prof_report {
  prof_report(int8_t num_args) : _num_calls(1), _num_args(num_args) {
    _unique_arg_evals = (int64_t *)calloc(num_args, sizeof(int64_t));
    _arg_evals = (int64_t *)calloc(num_args, sizeof(int64_t));
  }

  // ~prof_report() {
  //   free(_unique_arg_evals);
  //   free(_arg_evals);
  // }

  int64_t _num_calls;
  int8_t _num_args;
  int64_t *_unique_arg_evals;
  int64_t *_arg_evals;
};

static bool initialized = false;

static std::map<callsite_id, std::unique_ptr<struct prof_report>>
    profile_info;
static std::stack<callsite_id> call_stack;
std::recursive_mutex wyinstr_mutex;
static char const *first_fun_name = "__wyinstr_pre_main";

extern "C" void __attribute__((noinline)) _wyinstr_init_prof() {
  std::lock_guard<std::recursive_mutex> lock(wyinstr_mutex);
  initialized = true;

  const callsite_id first = std::make_pair(first_fun_name, 0);
  profile_info.insert(
      std::make_pair(first, std::make_unique<struct prof_report>(2)));

  call_stack.push(first);

#ifdef DEBUG
  fprintf(stderr, "Initialized profiling! Top of stack: <%s, %li>\n",
          call_stack.top().first, call_stack.top().second);
#endif
}

extern "C" void __attribute__((noinline))
_wyinstr_init_call(const char *fun_name, int64_t callinstr_id,
                   int8_t num_args) {
  std::lock_guard<std::recursive_mutex> lock(wyinstr_mutex);
  if (!initialized) {
    return;
  }
#ifdef DEBUG
  fprintf(stderr, "Adding call from callsite <%s,%li> with %d arguments!\n",
          fun_name, callinstr_id, num_args);
#endif

  const callsite_id id = std::make_pair(fun_name, callinstr_id);

  // first time callsite is reached, instantiate new profile report
  if (!profile_info.count(id)) {
#ifdef DEBUG
    fprintf(stderr,
            "First call on this callsite!. Inserting profile report... Profile "
            "map size: %li\n",
            profile_info.size());
#endif
    profile_info.insert(
        std::make_pair(id, std::make_unique<struct prof_report>(num_args)));
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
  std::lock_guard<std::recursive_mutex> lock(wyinstr_mutex);
  if (!initialized) {
    return;
  }
#ifdef DEBUG
  fprintf(stderr, "Logging eval of arg: %d\n", arg_index);
#endif

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
  std::lock_guard<std::recursive_mutex> lock(wyinstr_mutex);
  if (!initialized) {
    return;
  }
#ifdef DEBUG
  fprintf(stderr, "Ending call from callsite <%s, %li>. Stack size before popping: %li\n",
          call_stack.top().first, call_stack.top().second, call_stack.size());
#endif
  call_stack.pop();
#ifdef DEBUG
  if (!call_stack.empty()) {
    fprintf(stderr, "New top: <%s, %li>.\n", call_stack.top().first,
            call_stack.top().second);
  }
#endif
}

extern "C" __attribute__((noinline)) void _wyinstr_dump(const char *mod_name) {
  std::lock_guard<std::recursive_mutex> lock(wyinstr_mutex);
  std::string filename = std::string(mod_name) + ".csv";
  FILE *outfile = fopen(filename.c_str(), "w");
  fprintf(outfile,
          "fun_name,call_id,total_calls,num_args,unique_evals,total_evals\n");
  for (auto &[key, value] : profile_info) {
    fprintf(outfile, "%s,%li,%li,%d,", key.first, key.second, value->_num_calls,
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
