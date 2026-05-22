use fastkv::{DB, Opts};

fn main() {
    let path = "/tmp/fastkv_rust_basic";
    std::fs::create_dir_all(path).unwrap();

    let mut opts = Opts::default();
    opts.sync_writes = false;

    let db = DB::open(path, opts).expect("gagal buka DB");

    /* operasi dasar */
    db.put(b"nama", b"FastKV").unwrap();
    db.put(b"bahasa", b"Rust").unwrap();
    db.put(b"versi", b"0.1.0").unwrap();

    let val = db.get(b"nama").unwrap();
    println!("nama  = {}", String::from_utf8_lossy(&val));

    db.delete(b"versi").unwrap();
    match db.get(b"versi") {
        Err(fastkv::Error::NotFound) => println!("versi sudah dihapus"),
        other => println!("hasil tak terduga: {:?}", other),
    }

    /* transaksi */
    {
        let txn = db.begin(false).unwrap();
        txn.put(b"txn:a", b"1").unwrap();
        txn.put(b"txn:b", b"2").unwrap();
        txn.commit().unwrap();
    }
    println!("txn:a = {}", String::from_utf8_lossy(&db.get(b"txn:a").unwrap()));

    /* cursor */
    {
        let txn = db.begin(true).unwrap();
        let mut cur = txn.cursor(true).unwrap();
        println!("--- scan semua kunci ---");
        for item in cur.iter() {
            let (k, v) = item.unwrap();
            println!("  {} = {}", String::from_utf8_lossy(&k), String::from_utf8_lossy(&v));
        }
    }

    /* JSON index — index dibuat dulu sebelum data dimasukkan */
    {
        let idx = db.json_index("by_role", "role").unwrap();

        db.put(b"u:1", b"{\"role\":\"admin\",\"name\":\"ali\"}").unwrap();
        db.put(b"u:2", b"{\"role\":\"user\",\"name\":\"budi\"}").unwrap();
        db.put(b"u:3", b"{\"role\":\"admin\",\"name\":\"citra\"}").unwrap();

        let admins = idx.lookup(b"admin").unwrap();
        println!("admin: {:?}", admins.iter().map(|k| String::from_utf8_lossy(k).to_string()).collect::<Vec<_>>());
        db.drop_index("by_role").unwrap();
    }

    /* stats */
    let s = db.stats().unwrap();
    println!("stats: {} kunci, {} txn committed", s.num_keys, s.num_txn_committed);

    std::fs::remove_dir_all(path).ok();
}
