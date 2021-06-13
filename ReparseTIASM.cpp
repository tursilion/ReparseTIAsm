// ReparseTIASM.cpp : This file contains the 'main' function. Program execution begins and ends there.
//
// This program reads in GCC assembly code, and reparses it for asm994a or xas. Since labels
// are not truncated to 6 characters, you can expect it NOT to work on TI assembler.
// All files are combined into a single file for simplicity.

#include <iostream>
#include <string>
#include <vector>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <ctype.h>
#include <Shlwapi.h>    // for StrStrI

using namespace std;

vector<string> refs;
vector<string> defs;
vector<string> equs;
vector<string> bsss;
vector<string> prgs;
vector<string> syms;

char curFileDat[5*1024*1024];     // 5MB adequate!!
int fpointer, fpsz;
int labelchange = 0;

char *getkeyword(const char *buf) {
    static char ret[128];

    if (*buf=='*') {
        // comments have no keyword
        strcpy(ret,"");
        return ret;
    }

    // find till the first whitespace...
    const char *p = buf;
    while (*p > ' ') ++p;
    // now skip till the first word
    while ((*p)&&(*p <= ' ')) ++p;

    // now copy it into the static return buffer so we can lc it
    strncpy(ret, p, 128);
    ret[127]='\0';
    _strlwr(ret);

    // and nul out the first whitespace
    char *p2=ret;
    while (*p2 > ' ') ++p2;
    *p2='\0';        // might already be NUL, that's okay

    return ret;
}

// get next line from fpointer, max size fpsz, in curFileDat
bool getNextLine(char *buf, int bufsz) {
    if (fpointer >= fpsz) {
        return false;
    }
    while ((fpointer < fpsz) && (bufsz>0) && (curFileDat[fpointer] != '\n')) {
        *(buf++) = curFileDat[fpointer++];
        bufsz--;
    }
    if ((fpointer < fpsz) && (bufsz > 0)) {
        // copy the line ending too
        *(buf++) = curFileDat[fpointer++];
        bufsz--;
    }
    if (bufsz == 0) {
        // we still need to nul terminate, lose the character
        --buf;
    }
    *buf = '\0';
    return true;
}

// we need to replace the named label with a new one - we'll just
// update the last letter or number, checking for collision, and
// flip between letter and number if needed. Note we assume we are
// working with lowercase but need to search the buffer case-insensitive
// labels end with space or colon (should be only space, here)
void replaceLabel(string &lblin) {
    bool found;
    string lbl = lblin;

    // figure out a new name for the label
    for (int cnt = 36; cnt > 0; --cnt) {
        lbl.back()++;
        if ((lbl.back()=='z'+1)||(lbl.back()=='Z'+1)) lbl.back()='0';
        if (lbl.back() == '9'+1) lbl.back()='a';

        found = false;
        for (string s: syms) {
            if (s == lbl) {
                found = true;
                break;
            }
        }
        if (!found) break;
    }
    if (found) {
        printf("Warning: unfixable label: '%s'\n", lbl.c_str());
        return;
    }

    // now replace all instances of lblin with lbl (same size!)
    // really, only need to replace the last character
    char *p = curFileDat;
    for (;;) {
        p = StrStrI(p, lblin.c_str());
        if (NULL == p) {
            break;
        }
        // make sure it's a label and not part of something else
        char c = *(p+lblin.length());
        if ((c<=' ')||(c==':')) {
            // this is valid
            *(p+lblin.length()-1) = lbl.back();
        }
        p+=lblin.length();
    }

    lblin = lbl;
}

// we need to check and add all the labels in curFileDat
// If there are duplicates, we need to rename them.
// TODO: renaming a label found in a def statement would
// probably cause a linker error, but I can't be arsed to
// search for that right now...
void CheckLabels() {
    fpointer = 0;
    char buf[1024];
    vector<string> oldsyms = syms;  // in case we need to restore the old state

    while (getNextLine(buf, sizeof(buf))) {
        if (isalpha(buf[0])) {
            // this is a label, collect it
            string label;
            int idx = 0;
            while (buf[idx] > ' ') label += tolower(buf[idx++]);

            // now check if the label exists
            for (string s : syms) {
                if (0 == s.compare(label)) {
                    // we merge equates, so don't replace the label if it's exactly the same
                    bool found = false;
                    for (string t : equs) {
                        if (t.compare(buf) == 0) {
                            found = true;
                            break;
                        }
                    }
                    if (!found) {
                        replaceLabel(label);
                        ++labelchange;
                        fpointer = 0;       // start over
                        syms = oldsyms;
                    }
                    break;
                }
            }

            // save it only when it's unique - getNextLine updates fpointer
            if (fpointer > 0) syms.push_back(label);
        }
    }
}

int main(int argc, char *argv[]) {
    if (argc < 3) {
        printf("Reparses TI GCC asm into asm for xas or asm994a.\n");
        printf("Pass all the files you want, last filename is output.\n");
        printf("Files are reformatted so refs and defs are at the top\n");
        printf("followed by data and finally code.\n");
        return 1;
    }

    // read all the input files
    for (int idx = 1; idx<argc-1; ++idx) {
        printf("Reading %s...\n", argv[idx]);
        FILE *fp = fopen(argv[idx], "r");
        if (NULL == fp) {
            printf("Open failed! Code %d\n", errno);
            return 1;
        }
        fpsz = fread(curFileDat, 1, sizeof(curFileDat), fp);
        if (fpsz == sizeof(curFileDat)) {
            printf("* Warning: File truncated!!\n");
            --fpsz;     // make room for the nul
        }
        fclose(fp);
        // since we're reading text, this should be the only nul
        curFileDat[fpsz] = '\0';

        // do a label collision fixup...
        CheckLabels();

        fpointer=0;
        while (fpointer < fpsz) {
            char buf[1024];
            if (!getNextLine(buf, sizeof(buf))) break;

            // all right, let's see where it goes...
            char *pKey = getkeyword(buf);

            if (0 == strcmp(pKey, "ref")) {
                // take any previous comments to non-comment or empty line
                while ((!prgs.empty())&&(prgs.back()[0] == '/')) {
                    string old = prgs.back();
                    prgs.pop_back();
                    refs.push_back(old);
                }
                // all on one line
                refs.push_back(buf);
            } else if (0 == strcmp(pKey, "def")) {
                // take any previous comments to non-comment or empty line
                while ((!prgs.empty())&&(prgs.back()[0] == '/')) {
                    string old = prgs.back();
                    prgs.pop_back();
                    defs.push_back(old);
                }
                // all on one line
                defs.push_back(buf);
            } else if (0 == strcmp(pKey, "equ")) {
                // take any previous comments to non-comment or empty line
                while ((!prgs.empty())&&(prgs.back()[0] == '/')) {
                    string old = prgs.back();
                    prgs.pop_back();
                    equs.push_back(old);
                }
                // all on one line
                equs.push_back(buf);
            } else if (0 == strcmp(pKey, "bss")) {
                // we need the label from the previous line
                string oldLabel = prgs.back();
                bsss.push_back(oldLabel);
                prgs.pop_back();

                // take any previous comments to non-comment or empty line
                while ((!prgs.empty())&&(prgs.back()[0] == '*')) {
                    string old = prgs.back();
                    prgs.pop_back();
                    bsss.push_back(old);
                }

                bsss.push_back(buf);
            } else if (0 == strcmp(pKey, ".section")) {
                // ignore
            } else if (0 == strcmp(pKey, "dseg")) {
                // ignore
            } else if (0 == strcmp(pKey, "pseg")) {
                // ignore
            } else if (0 == strcmp(pKey, ".type")) {
                // ignore
            } else if (0 == strcmp(pKey, ".size")) {
                // ignore
            } else if (0 == strcmp(pKey, "even")) {
                // we'll output new evens as needed, but in case of text or bss
                // output this one too, as long as the last one wasn't
                // warning: after this line pKey is invalid
                if ((prgs.empty())||(0 != strcmp(getkeyword(prgs.back().c_str()), "even"))) {
                    prgs.push_back(buf);
                }
            } else {
                // just dump to prgs
                prgs.push_back(buf);
            }
        }
    }   // next file

    // check for errors
    // refs probably aren't needed, but what we SHOULD do is check that
    // every ref has a matching def. TODO
    int ret = 0;

    // check for duplicate DEFs lines - this likely means an import error
    for (unsigned int idx=0; idx<defs.size(); ++idx) {
        for (unsigned int i2=idx+1; i2<defs.size(); ++i2) {
            if (defs[idx] == defs[i2]) {
                printf("ERROR: duplicate definition of '%s'\n", defs[idx].c_str());
            }
        }
    }

    // now, exactly duplicate equates are okay, it's a side effect of how the modules are made
    for (unsigned int idx=0; idx<equs.size(); ++idx) {
        for (unsigned int i2=idx+1; i2<equs.size(); ++i2) {
            if (equs[idx] == equs[i2]) {
                equs[i2] = "";  // will print empty string, which is fine
            }
        }
    }


    // now we should be able to just write out the result
    FILE *fp = fopen(argv[argc-1], "w");
    if (NULL == fp) {
        printf("Failed to open output '%s'\n", argv[argc-1]);
        return 1;
    }
    printf("Writing output '%s'\n", argv[argc-1]);

    // Don't emit the refs, they piss off asm994a
    //for (string s : refs) {
    //    fprintf(fp, "%s", s.c_str());
    //}

    // let the user enable the defs if they need them
    fprintf(fp, "* enable these DEFs if you need them\n");
    for (string s : defs) {
        fprintf(fp, "*%s", s.c_str());
    }
    
    // for bss, we want to force even offset and also reset even
    // after any odd counts (gcc aligned all data even)
    fprintf(fp, "\n    even\n");
    for (string s : bsss) {
        fprintf(fp, "%s", s.c_str());
        for (char &c : s) {
            c = tolower(c);
        }
        int p = s.find("bss");
        if (string::npos != p) {
            int a = atoi(s.c_str()+p+4);
            if (a&1) {
                fprintf(fp, "\n    even\n");
            }
        }
    }

    fprintf(fp, "\n");
    for (string s : equs) {
        fprintf(fp, "%s", s.c_str());
    }

    // one more even before program
    fprintf(fp, "\n    even\n");
    for (string s : prgs) {
        fprintf(fp, "%s", s.c_str());
    }

    fclose(fp);

    if (labelchange > 0) printf("%d labels changed out of %d\n", labelchange, syms.size());
    printf("** DONE **\n");

    return ret;
}
