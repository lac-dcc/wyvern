import json
import numpy
import sys
from scipy import stats

wyvern_data = []
o3_data = []

wyvern_final = {}
o3_final = {}

def filter_data(baseline_tests, comparison_tests):
	to_keep = []
	for baseline_test in baseline_tests:
		for comparison_test in comparison_tests:
			if baseline_test['name'] == comparison_test['name']:
				to_keep.append(baseline_test)
				break
	return to_keep
			

def main():
	if len(sys.argv) < 4:
		print(f"Usage: python3 {sys.argv[0]} <baseline> <comparison> <num_runs>")
		quit()

	baseline_name = sys.argv[1]
	comparison_name = sys.argv[2]
	num_runs = int(sys.argv[3])

	for n in range(1, num_runs+1):
		wyvern_f = open(comparison_name + str(n) + ".json")
		o3_f = open(baseline_name + str(n) + ".json")
		wyvern_json = json.load(wyvern_f)
		o3_json = json.load(o3_f)

		o3_tests_sorted = sorted(o3_json['tests'], key = lambda d: d['name'])
		wyvern_tests_sorted = sorted(wyvern_json['tests'], key = lambda d: d['name'])
		o3_tests_sorted = filter_data(o3_tests_sorted, wyvern_tests_sorted)
		wyvern_tests_sorted = filter_data(wyvern_tests_sorted, o3_tests_sorted)

		print(f"Loading data from {comparison_name}{str(n)}.json", file=sys.stderr)
		print(f"Loading data from {baseline_name}{str(n)}.json", file=sys.stderr)
		wyvern_data.append(wyvern_tests_sorted)
		o3_data.append(o3_tests_sorted)
		
	print("bench\twyv_time\twyv_var\twyv_size\twyv_comp_time\to3_time\to3_var\to3_size\to3_comp_time\ttval\tpval\topt_had_effect\tsignif")
	for i, test in enumerate(wyvern_data[0]):
		if o3_data[0][i]['code'] == "FAIL" or o3_data[0][i]['code'] == "NOEXE":
			print(f"Test {test['name']} failed for O3!", file=sys.stderr)
			continue

		if test['code'] == "FAIL":
			print(f"Test {test['name']} failed for Wyvern!", file=sys.stderr)
			continue

		if test['code'] == "NOEXE":
			print(f"Test {test['name']} has no exe for Wyvern!", file=sys.stderr)
			continue

		if test['name'] != o3_data[0][i]['name']:
			print("Test from O3 does not match test from comparison! Quitting...")
			exit()

		if 'exec_time' not in test['metrics']:
			continue

		wyvern_runs = []
		o3_runs = []

		for j in range (0, num_runs):
			wyvern_runs.append(wyvern_data[j][i]['metrics'])
			o3_runs.append(o3_data[j][i]['metrics'])


		name = test['name']
		only_exec = 'compile_time' not in test['metrics']

		o3_exec_times = [ x['exec_time'] for x in o3_runs ]
		o3_exec_time = numpy.mean(o3_exec_times)
		o3_variance = numpy.var(o3_exec_times)

		wyvern_exec_times = [ x['exec_time'] for x in wyvern_runs ]
		wyvern_exec_time = numpy.mean(wyvern_exec_times)
		wyvern_variance = numpy.var(wyvern_exec_times)

		o3_compile_time = "n/a"
		o3_code_size = "n/a"
		o3_hash = "n/a"
		wyvern_compile_time = "n/a"
		wyvern_code_size = "n/a"
		wyvern_hash = "n/a"
		opt_had_effect = "n/a"
		if not only_exec:
			o3_compile_time = o3_runs[0]['compile_time']
			o3_code_size = o3_runs[0]['size..text']
			o3_hash = o3_runs[0]['hash']
			wyvern_compile_time = wyvern_runs[0]['compile_time']
			wyvern_code_size = wyvern_runs[0]['size..text']
			wyvern_hash = wyvern_runs[0]['hash']
			opt_had_effect = o3_hash != wyvern_hash
		
		tval, pval = stats.ttest_ind(wyvern_exec_times, o3_exec_times)
		statistically_significant = pval <= 0.05

		out = []
		out.append(name)
		out.append(f'{wyvern_exec_time:.10f}')
		out.append(f'{wyvern_variance:.10f}')
		out.append(str(wyvern_code_size))
		out.append(str(wyvern_compile_time))
		out.append(f'{o3_exec_time:.10f}')
		out.append(f'{o3_variance:.10f}')
		out.append(str(o3_code_size))
		out.append(str(o3_compile_time))
		out.append(f'{tval:.10f}')
		out.append(f'{pval:.10f}')
		out.append(str(opt_had_effect))
		out.append(str(statistically_significant))
		print('\t'.join(out))
if __name__ == "__main__":
	main()
