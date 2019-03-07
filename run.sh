INPUT="${1}"
CLANG="/Users/lac/Work/llvm701-build/bin/clang";
CLANGPP="/Users/lac/Work/llvm701-build/bin/clang++";
OPT="/Users/lac/Work/llvm701-build/bin/opt";
LDC="/Users/lac/Work/ldc/build/bin/ldc2"

LIB_DIR="/Users/lac/Work/dlang/wyvern/lib"
WYV_PASS="/Users/lac/Work/dlang/wyvern/lib/analyses/LLVMWyvern.dylib"

echo "Input file: ${INPUT}"

if [ ! -f ${INPUT} ]
	then
		echo "Input file does not exist!"
		exit
fi

cd $LIB_DIR
make

cd -

FILE_EXT="${INPUT##*.}"

if [ "${FILE_EXT}" = "c" ]; then
	echo "Input file is in C!"
	COMP_FUNC="compileC"

elif [ "${FILE_EXT}" = "cpp" ]; then
	echo "Input file is in C++!"
	COMP_FUNC="compileCPP"

elif [ "${FILE_EXT}" = "d" ]; then
	echo "Input file is in D!"
	COMP_FUNC="compileD"
else
	echo "Unknown file extension!"
	exit
fi

function compileC {
	$CLANG -emit-llvm -S -c -O0 -Xclang -disable-O0-optnone ${1} -o ${2}
}

function compileCPP {
	$CLANGPP -emit-llvm -S -c -O0 -Xclang -disable-O0-optnone ${1} -o ${2}
}

function compileD {
	$LDC -output-ll -O0 ${1} -of ${2}
}

EMPTY_NAME="${INPUT%.*}"
BC_NAME="${EMPTY_NAME}.bc"
RBC_NAME="${EMPTY_NAME}.rbc"

echo "Generating ${BC_NAME}"
$COMP_FUNC "${INPUT}" "${BC_NAME}"

echo "Generating ${RBC_NAME}"
$OPT -S -mem2reg ${BC_NAME} -o ${RBC_NAME}

echo "Running Wyvern analysis..."
$OPT -load $WYV_PASS -wyvern ${RBC_NAME} -o ${RBC_NAME}
