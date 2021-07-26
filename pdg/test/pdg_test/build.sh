clang -emit-llvm -S -g *.c

for f in *.ll; do
    llvm-as $f
done

mv *.ll ./llvm_ll
mv *.bc ./bitcode
