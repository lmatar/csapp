#include <stdio.h>

int bar1(int x) {
  return x;
}

int bar2(int x, int y) {
  return x + y;
}


int bar3(int x, int y, int z) {
  return x + y + z;
}

int bar4(int x, int y, int z, int w) {
  return x + y + z + w;
}

void foo() {
  int x = 3;
  bar1(x);
  int y = 4;
  bar2(x,y);
  int z = 5;
  bar3(x,y,z);
  int w = 6;
  bar4(x,y,z,w);
}


void input() {
  char s[] = "1 a 1";
  int x,y;
  char c;
  int v = sscanf(s, "%d %c %d", &x,&c,&y);
  printf("%d: %d %c %d\n", v, x,c,y);
}

int main() {
  input();
  return 0;
}