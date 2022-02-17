TEST_FILES=$(find . -name "*test*.c")

memo=${1}
use_clang=${2}

MEMO_FLAG="false"
USE_CLANG="false"
if [ "$memo" = "1" ]; then
	MEMO_FLAG="true"
fi

if [ "$use_clang" = "1" ]; then
	USE_CLANG="true"
fi

echo "MEMO_FLAG=${MEMO_FLAG}"
echo "CLANG=${USE_CLANG}"


echo "COMMAND: $COMMAND"

for f in ${TEST_FILES}; do
	echo ========= Running test ${f} =========
	if [ "$USE_CLANG" = "true" ]; then
		COMMAND=$(cat <<-END
		clang -flegacy-pass-manager -Xclang -load -Xclang ../build/passes/libWyvern.so ${f} -O3 -mllvm -stats -o test
		END)
	else
		COMMAND=$(cat <<-END
			clang -S -c -emit-llvm -Xclang -disable-O0-optnone ${f} -o test.ll;
			opt -load ~/Work/wyvern/build/passes/libWyvern.so -S
			-mem2reg `#promote to registers`
			-mergereturn  `#merge all returns`
			-function-attrs  `#add fn attributes`
			-loop-simplify  `#simplifies loops to more canonical forms`
			-lcssa `#gates loops with phi functions for variable uses`
			-enable-new-pm=0 `#disables new passManager`
			-lazify-callsites  `#calls the Wyvern optimization pass`
			-wylazy-memo=${MEMO_FLAG} `#argument do determine to use memoization`
			-stats `#print llvm pass stats`
			test.ll `#input file`
			-o test_lazyfied.ll `#output file`
			END)
	fi
	$COMMAND
	echo ========= Done with ${f}! =========
	if [[ "$RUN" = "false" ]]; then
		echo "Not running test!"
		continue
	fi
done

