FROM gcc:14

WORKDIR /app

COPY backend.cpp .
COPY index.html .
COPY httplib.h .

RUN g++ -std=c++17 -O2 -Wall -Wextra -pthread backend.cpp -o server

ENV PORT=8080
EXPOSE 8080

CMD ["./server"]