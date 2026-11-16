//! ed3k-hashbody: writes the hash body of files (docs/ed3k-spec/extensions/http-source.md
//! §3.1) for `wh=` in ed3k links.
//!
//!   ed3k-hashbody <out-dir> <file>...
//!
//! Per file: `<out-dir>/<file name>.ed3k-hashes` (none for a file of one block), and a line
//! `<hex root>  <file name>` on stdout like b3sum, so CI can check the root against b3sum.
//!
//! Tree: level 0 = chaining values of 256 KiB blocks, each level above pairs nodes with
//! BLAKE3's parent function, a partnerless node goes up unchanged; root = top two merged
//! with the ROOT flag. Body = levels L−1 down to 0, 32 bytes per node, root not stored.

use std::fs::File;
use std::io::{self, Read, Write};
use std::path::{Path, PathBuf};
use std::process::ExitCode;

use blake3::hazmat::{ChainingValue, HasherExt, Mode, merge_subtrees_non_root, merge_subtrees_root};

/// ed3k block (Protocol.md §7.2).
const BLOCK: usize = 256 * 1024;
const SUFFIX: &str = ".ed3k-hashes";

fn main() -> ExitCode {
    let args: Vec<String> = std::env::args().skip(1).collect();
    let Some((out_dir, files)) = args.split_first().filter(|(_, f)| !f.is_empty()) else {
        eprintln!("usage: ed3k-hashbody <out-dir> <file>...");
        return ExitCode::from(2);
    };
    let out_dir = PathBuf::from(out_dir);
    for file in files {
        if let Err(e) = one(&out_dir, Path::new(file)) {
            eprintln!("ed3k-hashbody: {file}: {e}");
            return ExitCode::FAILURE;
        }
    }
    ExitCode::SUCCESS
}

fn one(out_dir: &Path, path: &Path) -> io::Result<()> {
    let name = path.file_name().and_then(|n| n.to_str()).ok_or_else(|| io::Error::other("file name not UTF-8"))?;
    let (root, levels) = tree(path)?;
    if !levels.is_empty() {
        let mut body = Vec::with_capacity(levels.iter().map(Vec::len).sum::<usize>() * 32);
        for level in levels.iter().rev() {
            for cv in level {
                body.extend_from_slice(cv);
            }
        }
        std::fs::write(out_dir.join(format!("{name}{SUFFIX}")), body)?;
    }
    let hex: String = root.iter().map(|b| format!("{b:02x}")).collect();
    writeln!(io::stdout(), "{hex}  {name}")
}

/// Root and the stored levels 0 … L−1 (empty for one block).
fn tree(path: &Path) -> io::Result<([u8; 32], Vec<Vec<ChainingValue>>)> {
    let mut f = File::open(path)?;
    let mut buf = vec![0u8; BLOCK];
    let mut cvs: Vec<ChainingValue> = Vec::new();
    let mut first_block: Option<Vec<u8>> = None;
    let mut offset = 0u64;
    loop {
        let n = read_full(&mut f, &mut buf)?;
        if n == 0 && offset > 0 {
            break;
        }
        let data = buf.get(..n).ok_or_else(|| io::Error::other("read length"))?;
        if offset == 0 {
            first_block = Some(data.to_vec());
        }
        let mut h = blake3::Hasher::new();
        h.set_input_offset(offset);
        h.update(data);
        cvs.push(h.finalize_non_root());
        offset += n as u64;
        if n < BLOCK {
            break;
        }
    }
    if cvs.len() == 1 {
        // one block (or empty): the plain hash is the root, no body
        let root = blake3::hash(first_block.as_deref().unwrap_or_default());
        return Ok((*root.as_bytes(), Vec::new()));
    }
    let mut levels = vec![cvs];
    loop {
        let below = levels.last().ok_or_else(|| io::Error::other("no level"))?;
        if below.len() == 2 {
            let root = merge_subtrees_root(&below[0], &below[1], Mode::Hash);
            return Ok((*root.as_bytes(), levels));
        }
        let up: Vec<ChainingValue> = below
            .chunks(2)
            .map(|p| match p {
                [l, r] => merge_subtrees_non_root(l, r, Mode::Hash),
                [single] => *single,
                _ => unreachable!("chunks(2)"),
            })
            .collect();
        levels.push(up);
    }
}

fn read_full(f: &mut File, buf: &mut [u8]) -> io::Result<usize> {
    let mut got = 0;
    while got < buf.len() {
        match f.read(&mut buf[got..])? {
            0 => break,
            n => got += n,
        }
    }
    Ok(got)
}
