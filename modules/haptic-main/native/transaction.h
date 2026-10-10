// Included after Frozen: all tracees are stopped until the transaction completes.
static void change(pid_t pid, Memory& mem, uintptr_t base, const Frozen& frozen, uint32_t after) {
    for(const auto& thread:frozen.threads) {
        user_pt_regs regs{}; iovec io{&regs,sizeof(regs)};
        require(ptrace(PTRACE_GETREGSET,thread.id,reinterpret_cast<void*>(NT_PRSTATUS),&io)==0,
                "Cannot inspect stopped thread registers");
        require(!(regs.pc>=base+TUNE && regs.pc<base+TUNE+sizeof(TUNING)),
                "Thread in tuning block; retry while idle; no writes");
    }
    struct Word { uintptr_t address; uint64_t old, next; };
    std::vector<Word> words;
    auto prepare=[&](uintptr_t offset,const unsigned char* data,size_t n) {
        uintptr_t first=(base+offset)&~uintptr_t(7), end=base+offset+n;
        for(uintptr_t addr=first;addr<end;addr+=8) {
            Word w{addr,0,0}; mem.get(addr,&w.old,8); w.next=w.old;
            for(size_t j=0;j<8;++j) if(addr+j>=base+offset && addr+j<end)
                reinterpret_cast<unsigned char*>(&w.next)[j]=data[addr+j-base-offset];
            words.push_back(w);
        }
    };
    uint32_t route=after?MAIN:STOCK;
    prepare(SITE,reinterpret_cast<unsigned char*>(&route),4);
    prepare(TUNE,after?TUNING:EXPECTED+TUNE-FUNCTION,sizeof(TUNING));
    try {
        for(const auto& w:words)
            require(ptrace(PTRACE_POKETEXT,pid,reinterpret_cast<void*>(w.address),reinterpret_cast<void*>(w.next))==0,"Patch write failed");
        require(validate(mem,base)==after,"Patch readback failed");
    } catch(...) {
        bool restored=true;
        for(const auto& w:words) {
            if(ptrace(PTRACE_POKETEXT,pid,reinterpret_cast<void*>(w.address),reinterpret_cast<void*>(w.old))!=0) restored=false;
            uint64_t check=0;
            try { mem.get(w.address,&check,8); if(check!=w.old) restored=false; } catch(...) { restored=false; }
        }
        if(!restored) fprintf(stderr,"RESTORE FAILED: keep module disabled and reboot\n");
        throw;
    }
}
