TEST_FILES=$(find . -name "*test*.c")

for f in ${TEST_FILES}; do
	echo ========= Running test ${f} =========
	clang -S -c -emit-llvm -Xclang -disable-O0-optnone $f -o test.ll
	opt -load ~/Work/wyvern/build/PDG/libPDG.so -load ~/Work/wyvern/build/passes/libWyvern.so -S -mem2reg -mergereturn -function-attrs -loop-simplify -lcssa -lazify-callsites -enable-new-pm=0 -wylazy-memo=false test.ll -stats -debug -o test_lazyfied.ll
done

