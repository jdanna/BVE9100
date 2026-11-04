// Post-import script: adds the .data block from a Sony SRMS .abs file, applies symbols,
// analyzes, and dumps decompiled C + disassembly listing.
// args: <absfile> <symfile|-> <outprefix>
import ghidra.app.script.GhidraScript;
import ghidra.app.decompiler.*;
import ghidra.program.model.address.*;
import ghidra.program.model.listing.*;
import ghidra.program.model.mem.*;
import ghidra.program.model.symbol.*;
import ghidra.util.task.ConsoleTaskMonitor;
import java.io.*;
import java.nio.file.*;
import java.util.*;

public class AbsFinish extends GhidraScript {
    static long be32(byte[] b, int o) { return ((b[o]&0xffL)<<24)|((b[o+1]&0xffL)<<16)|((b[o+2]&0xffL)<<8)|(b[o+3]&0xffL); }
    static int be16(byte[] b, int o) { return ((b[o]&0xff)<<8)|(b[o+1]&0xff); }

    @Override
    public void run() throws Exception {
        String[] a = getScriptArgs();
        byte[] f = Files.readAllBytes(Paths.get(a[0]));
        String symPath = a[1];
        String outPrefix = a[2];
        int symsz = be16(f, 2);
        long tsz = be32(f, 4), dsz = be32(f, 8);
        long taddr = be32(f, 0x14), daddr = be32(f, 0x18);
        long entry = be32(f, 0x20);
        long dataInFile = dsz;
        Memory mem = currentProgram.getMemory();
        AddressSpace sp = currentProgram.getAddressFactory().getDefaultAddressSpace();
        Address dA = sp.getAddress(daddr);
        if (mem.getBlock(dA) == null && dataInFile > 0) {
            MemoryBlock db = mem.createInitializedBlock(".data", dA,
                new ByteArrayInputStream(f, (int)(0x1c + tsz), (int)dataInFile), dataInFile, monitor, false);
            db.setWrite(true); db.setRead(true); db.setExecute(false);
        }
        // bss tail
        MemoryBlock tb = mem.getBlock(sp.getAddress(taddr));
        if (tb != null) { tb.setName(".text"); tb.setExecute(true); tb.setWrite(false); }

        SymbolTable st = currentProgram.getSymbolTable();
        FunctionManager fm = currentProgram.getFunctionManager();
        List<Address> funcAddrs = new ArrayList<>();
        if (!symPath.equals("-")) {
            for (String line : Files.readAllLines(Paths.get(symPath))) {
                String[] p = line.trim().split("\\s+");
                if (p.length < 3) continue;
                long addr = Long.parseLong(p[0], 16); int typ = Integer.parseInt(p[1], 16);
                String name = p[2];
                if (name.startsWith("_")) name = name.substring(1);
                Address ad = sp.getAddress(addr);
                if (!mem.contains(ad)) continue;
                try { st.createLabel(ad, name, SourceType.IMPORTED); } catch (Exception e) {}
                if (typ == 0x0d) funcAddrs.add(ad);
            }
        }
        if (entry != 0 && mem.contains(sp.getAddress(entry))) {
            funcAddrs.add(sp.getAddress(entry));
            try { st.createLabel(sp.getAddress(entry), "ENTRY", SourceType.IMPORTED); } catch (Exception e) {}
            addEntryPoint(sp.getAddress(entry));
        }
        analyzeAll(currentProgram);
        Listing lst0 = currentProgram.getListing();
        for (Address ad : funcAddrs) {
            if (fm.getFunctionAt(ad) != null) continue;
            if (fm.getFunctionContaining(ad) != null) continue;
            if (lst0.getDefinedDataAt(ad) != null) continue;
            // only treat as code if it starts with a plausible prologue/instruction
            int w = mem.getShort(ad) & 0xffff;
            boolean plausible = (w == 0x4e56) || (w == 0x48e7) || ((w & 0xf000) == 0x2000) || ((w & 0xf000) == 0x4000) || ((w & 0xf000) == 0x6000) || (w == 0x4e75) || ((w & 0xff00) == 0x7000) || ((w & 0xf000) == 0x1000) || ((w & 0xf000) == 0x3000) || (w==0x4eb9) || (w==0x4ef9) || ((w&0xfff8)==0x4e50);
            if (!plausible) continue;
            disassemble(ad); createFunction(ad, null);
        }
        analyzeAll(currentProgram);

        // Dump decompiled C
        DecompInterface di = new DecompInterface();
        di.toggleCCode(true); di.toggleSyntaxTree(false); di.setSimplificationStyle("decompile");
        di.openProgram(currentProgram);
        try (PrintWriter pw = new PrintWriter(new FileWriter(outPrefix + ".c"))) {
            FunctionIterator it = fm.getFunctions(true);
            while (it.hasNext()) {
                Function fn = it.next();
                DecompileResults r = di.decompileFunction(fn, 60, monitor);
                pw.println("// ==== " + fn.getName() + " @ " + fn.getEntryPoint());
                if (r != null && r.decompileCompleted() && r.getDecompiledFunction() != null) pw.println(r.getDecompiledFunction().getC());
                else pw.println("// decompile failed: " + (r == null ? "null" : r.getErrorMessage()));
            }
        }
        di.dispose();
        // Dump listing (disassembly with labels)
        try (PrintWriter pw = new PrintWriter(new FileWriter(outPrefix + ".asm"))) {
            Listing lst = currentProgram.getListing();
            InstructionIterator ii = lst.getInstructions(true);
            while (ii.hasNext()) {
                Instruction in = ii.next();
                Symbol s = st.getPrimarySymbol(in.getAddress());
                if (s != null) pw.println(s.getName() + ":");
                pw.println("  " + in.getAddress() + "  " + in.toString());
            }
        }
        // Dump function list
        try (PrintWriter pw = new PrintWriter(new FileWriter(outPrefix + ".funcs"))) {
            FunctionIterator it = fm.getFunctions(true);
            while (it.hasNext()) { Function fn = it.next(); pw.println(fn.getEntryPoint() + " " + fn.getName() + " " + fn.getBody().getNumAddresses()); }
        }
        println("AbsFinish done: " + outPrefix);
    }
}
