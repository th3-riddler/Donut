COMPILER = g++

CFLAGS = -Wall -g -Wno-char-subscripts -Wno-write-strings -Ofast -pthread -fomit-frame-pointer -DUSE_SSE41 -msse4.1 -DUSE_SSSE3 -mssse3 -DUSE_SSE2 -msse2 -DUSE_SSE -msse -DUSE_AVX2 -mavx2 -DUSE_BMI2 -mbmi2 -DUSE_POPCNT -mpopcnt

TARGET = donut

SRCS = main.cpp Chessboard/chessboard.cpp Move/move.cpp Search/search.cpp Evaluation/evaluation.cpp nnueEval/nnueEval.cpp nnue/misc.cpp nnue/bitboard.cpp nnue/evaluate.cpp nnue/position.cpp nnue/probe.cpp nnue/nnue/features/half_ka_v2_hm.cpp nnue/nnue/evaluate_nnue.cpp Reader/reader.cpp Polyglot/polyglot.cpp

OBJS = main.o Chessboard/chessboard.o Move/move.o Search/search.o Evaluation/evaluation.o nnueEval/nnueEval.o nnue/misc.o nnue/bitboard.o nnue/evaluate.o nnue/position.o nnue/probe.o nnue/nnue/features/half_ka_v2_hm.o nnue/nnue/evaluate_nnue.o Reader/reader.o Polyglot/polyglot.o

# Precompiled header
PCH = Macros/precompiled.hpp
PCH_GCH = $(PCH).gch

all: $(TARGET)

$(TARGET): $(OBJS)
	$(COMPILER) $(CFLAGS) -o $(TARGET) $(OBJS)
	rm -f $(OBJS)

%.o: %.cpp $(PCH_GCH)
	$(COMPILER) $(CFLAGS) -c $< -o $@

$(PCH_GCH): $(PCH)
	$(COMPILER) $(CFLAGS) -c $<

run: $(TARGET)
	./$(TARGET)

clean:
	rm -f $(OBJS) $(TARGET) $(PCH_GCH)