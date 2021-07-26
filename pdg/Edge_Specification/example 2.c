#include <stdio.h>
#include <stdlib.h>
#include <string.h>

char __attribute__((annotate("blue"))) *key ;
char *ciphertext;
static unsigned int i;

void greeter (char *str, int* s) {
    char* p = str;
    static int sample = 1;
    printf("%s\n", p);
    printf(", welcome!\n");
    *s = 15;
}

void initkey (int sz) {
	key = (char *) (malloc (sz));
	// init the key randomly; code omitted
	for (i=0; i<sz; i++) key[i]= 1;
}

int __attribute__((annotate("green"))) encrypt (char *plaintext, int sz) {
	ciphertext = (char *) (malloc (sz));
	for (i=0; i<sz; i++)
		ciphertext[i]=plaintext[i] ^ key[i];
    return sz;
}

int main (){
   	int age = 10;
	char __attribute__((annotate("orange")))username[20];
        char text[1024];
	printf("Enter username: ");
	scanf("%19s",username);
	greeter(username, &age);
	printf("Enter plaintext: ");
	scanf("%1023s",text);

	initkey(strlen(text));
	int sz = encrypt(text, strlen(text));
	printf("Cipher text: ");
	for (i=0; i<strlen(text); i++)
		printf("%x ",ciphertext[i]);
    	printf("encryption length: %d", sz);
    	return 0;
}
