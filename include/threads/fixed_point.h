#include <stdio.h>
#define F (1<<14) // fixed point 1 
#define INT_MAX ((1<<31) - 1)
#define INT_MIN (-(1<<31))

// 17.14 format - 고정 소수점 표현 방식
// 총 32 bit - 17 bit(정수 부분), 14 bit(소수 부분) 
// x, y : fixed-point numbers
// n은 정수

int int_to_fp(int n); /* integer를 fixed point로 전환*/
int fp_to_int_round(int x); /*fixed point를 int로, 반올림*/
int fp_to_int(int x); /*fixed point를 int로, 버림*/

int add_fp(int x, int y); /*fixed point의 덧셈*/
int add_mixed(int x, int n); /*fixed point와 int의 덧셈*/
int sub_fp(int x, int y); /*fixed point의 뺄셈*/
int sub_mixed(int x, int n); /*fixed point와 int의 뺄셈*/
int mult_fp(int x, int y); /*fixed point의 곱셈*/
int mult_mixed(int x, int n); /*fixed point와 int의 곱셈*/
int divide_fp(int x, int y); /*fixed point의 나눗셈 (x/y)*/
int divide_mixed(int x, int y); /*fixed point와 int의 나눗셈 (x/n)*/


int int_to_fp(int n) {
    return n * F; 
}

int fp_to_int_round(int x){
    if (x>=0)   
        return (x + F / 2) / F; 
    else 
        return (x - F / 2) / F;
}

int fp_to_int(int x) {
    return x / F;
}



int add_fp(int x, int y){
    return x + y;
}

int add_mixed(int x, int n){
    return x + n * F; 
}

int sub_fp(int x, int y){
    return  x - y;
}

int sub_mixed(int x, int n){
    return x - n*F;
}

int mult_fp(int x, int y){
    return ((int64_t)x)* y / F; // 두 고정 소수점 수 x, y를 곱함 -> 곱셈 결과는 F로 한번 나눠주어야 함
}

int mult_mixed(int x, int n){
    return x * n; 
}

int divide_fp(int x, int y){
    return ((int64_t)x)* F / y; // 두 고정 소수점 수 x와 y를 나눔 -> x를 먼저 F로 곱해줘야 함
}

int divide_mixed(int x, int n){
    return x / n;
}