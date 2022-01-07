TEST_FILES=$(find . -name "*test*.c")

memo=${1}
run=${2}

MEMO_FLAG="false"
RUN="false"
if [ "$memo" = "1" ]; then
	MEMO_FLAG="true"
fi

if [ "$run" = "1" ]; then
	RUN="true"
fi

echo "MEMO_FLAG=${MEMO_FLAG}"
echo "RUN=${RUN}"

for f in ${TEST_FILES}; do
	echo ========= Running test ${f} =========
	clang -S -c -emit-llvm -Xclang -disable-O0-optnone $f -o test.ll
	opt -load ~/Work/wyvern/build/passes/libWyvern.so -S \
		-mem2reg `#promote to registers` \
		-mergereturn  `#merge all returns` \
		-function-attrs  `#add fn attributes` \
		-loop-simplify  `#simplifies loops to more canonical forms` \
		-lcssa `#gates loops with phi functions for variable uses` \
		-enable-new-pm=0 `#disables new passManager` \
		-lazify-callsites  `#calls the Wyvern optimization pass` \
		-wylazy-memo=${MEMO_FLAG} `#argument do determine to use memoization` \
		-stats `#print llvm pass stats` \
		test.ll `#input file` \
		-o test_lazyfied.ll `#output file`
	echo ========= Done with ${f}! =========
	if [[ "$RUN" = "false" ]]; then
		echo "Not running test!"
		continue
	fi
done

