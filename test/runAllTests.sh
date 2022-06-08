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

for f in ${TEST_FILES}; do
	echo "========= Running test ${f} ========="
	if [ "$USE_CLANG" = "true" ]; then
		clang -flegacy-pass-manager -flto -Xclang -disable-O0-optnone -fuse-ld=lld -Wl,-mllvm=-load=../build/passes/libWyvern.so ${f} -O0 -Wl,-mllvm=-stats -o test
	else
		clang -S -c -emit-llvm -Xclang -disable-O0-optnone ${f} -o test.ll
		opt -load ../build/passes/libWyvern.so -S -mem2reg -mergereturn -function-attrs -loop-simplify -lcssa -enable-new-pm=0 -lazify-callsites -wylazy-memo=${MEMO_FLAG} -instcombine -stats test.ll -o test_lazyfied.ll
	fi
done
