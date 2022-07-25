


type Trie struct {
	db* Database
		root node
		cachegen, cachelimit uint16
}
type node interface {
	fstring(string) string
		cache() (hashNode, bool)
		canUnload(cachegen, cachelimit uint16) bool
}

type(
	fullNode struct {
	Children[17]node // Actual trie node data to encode/decode (needs custom encoder)
		flags    nodeFlag
}
shortNode struct {
	Key[]byte
		Val   node
		flags nodeFlag
}
hashNode[]byte
valueNode[]byte
)

type nodeFlag struct {
	hash  hashNode // cached hash of the node (may be nil)
		gen   uint16   // cache generation counter
		dirty bool     // whether the node has changes that must be written to the database
}

func New(root common.Hash, db* Database) (*Trie, error) {
	if db == nil{
		panic("trie.New called without a database")
	}
	trie: = &Trie{
		db:           db,
		originalRoot : root,
	}
	if (root != common.Hash{}) && root != emptyRoot{
		rootnode, err : = trie.resolveHash(root[:], nil)
		if err != nil {
			return nil, err
		}
		trie.root = rootnode
	}
		return trie, nil
}

func(t* Trie) resolveHash(n hashNode, prefix[]byte) (node, error) {
	cacheMissCounter.Inc(1)

		hash : = common.BytesToHash(n)
		//通过hash解析出node的RLP值
		enc, err : = t.db.Node(hash)
		if err != nil || enc == nil{
			return nil,& MissingNodeError{NodeHash: hash, Path : prefix}
		}
			return mustDecodeNode(n, enc, t.cachegen), nil
}

func(t* Trie) Commit(onleaf LeafCallback) (root common.Hash, err error) {
	if t.db == nil{
		panic("commit called on trie with nil database")
	}
		hash, cached, err : = t.hashRoot(t.db, onleaf)
		if err != nil{
			return common.Hash{}, err
		}
			t.root = cached  // 更新了t.root,将节点的hash值放在了root节点的node.flag.hash中
			t.cachegen++
			return common.BytesToHash(hash.(hashNode)), nil
}

// 该函数返回的是根hash和存入了hash值的trie的副本
func(t* Trie) hashRoot(db* Database, onleaf LeafCallback) (node, node, error) {
	if t.root == nil{
		return hashNode(emptyRoot.Bytes()), nil, nil
	}
	h: = newHasher(t.cachegen, t.cachelimit, onleaf)
		defer returnHasherToPool(h)
		return h.hash(t.root, db, true)
}

func(h* hasher) hash(n node, db* Database, force bool) (node, node, error) {
	// If we're not storing the node, just hashing, use available cached data
	if hash, dirty : = n.cache(); hash != nil{
		if db == nil {
			return hash, n, nil
		}
		if n.canUnload(h.cachegen, h.cachelimit) {
			cacheUnloadCounter.Inc(1)
			return hash, hash, nil
		}
		if !dirty {
			return hash, n, nil
		}
	}
		// 对所有子节点进行处理,将本节点中所有的节点都替换为节点的hash
			collapsed, cached, err : = h.hashChildren(n, db)
			if err != nil{
				return hashNode{}, n, err
			}
				// 将hash和rlp编码的节点存入数据库
				hashed, err : = h.store(collapsed, db, force)
				if err != nil{
					return hashNode{}, n, err
				}

					cachedHash, _ : = hashed.(hashNode)
					switch cn : = cached.(type) {
					case* shortNode:
						cn.flags.hash = cachedHash
							if db != nil{
								cn.flags.dirty = false
							}
					case* fullNode:
						cn.flags.hash = cachedHash
							if db != nil{
								cn.flags.dirty = false
							}
					}
		return hashed, cached, nil
}

// decodeNode parses the RLP encoding of a trie node.
func decodeNode(hash, buf[]byte, cachegen uint16) (node, error) {
	if len(buf) == 0 {
		return nil, io.ErrUnexpectedEOF
	}
	elems, _, err : = rlp.SplitList(buf)
		if err != nil{
			return nil, fmt.Errorf("decode error: %v", err)
		}
	switch c, _ : = rlp.CountValues(elems); c{
	case 2:
		n, err : = decodeShort(hash, buf, elems, cachegen)
		return n, wrapError(err, "short")
	case 17:
		n, err : = decodeFull(hash, buf, elems, cachegen)
		return n, wrapError(err, "full")
	default:
		return nil, fmt.Errorf("invalid number of list elements: %v", c)
	}
}

func decodeShort(hash, buf, elems[]byte, cachegen uint16) (node, error) {
	// kbuf代表compact key
	// rest代表shortnode的value
	kbuf, rest, err : = rlp.SplitString(elems)
		if err != nil{
			return nil, err
		}
		flag: = nodeFlag{ hash: hash, gen : cachegen }
		key: = compactToHex(kbuf)
			if hasTerm(key) {
				// value node
				val, _, err : = rlp.SplitString(rest)
					if err != nil{
						return nil, fmt.Errorf("invalid value node: %v", err)
					}
						return &shortNode{ key, append(valueNode{}, val...), flag }, nil
			}
	r, _, err : = decodeRef(rest, cachegen)
		if err != nil{
			return nil, wrapError(err, "val")
		}
			return &shortNode{ key, r, flag }, nil
}
func decodeRef(buf[]byte, cachegen uint16) (node, []byte, error) {
	kind, val, rest, err : = rlp.Split(buf)
		if err != nil{
			return nil, buf, err
		}
			switch {
			case kind == rlp.List:
				// 'embedded' node reference. The encoding must be smaller
				// than a hash in order to be valid.
				// len(buf) - len(rest)得到的结果应该是类型加内容的长度
				if size : = len(buf) - len(rest); size > hashLen{
					err: = fmt.Errorf("oversized embedded node (size is %d bytes, want size < %d)", size, hashLen)
					return nil, buf, err
				}
					n, err : = decodeNode(nil, buf, cachegen)
					return n, rest, err
			case kind == rlp.String && len(val) == 0:
				// empty node
				return nil, rest, nil
			case kind == rlp.String && len(val) == 32:
				return append(hashNode{}, val...), rest, nil
			default:
				return nil, nil, fmt.Errorf("invalid RLP string size %d (want 0 or 32)", len(val))
			}
}


func decodeFull(hash, buf, elems[]byte, cachegen uint16) (*fullNode, error) {
n: = &fullNode{ flags: nodeFlag{hash: hash, gen : cachegen} }
	for i : = 0; i < 16; i++ {
		cld, rest, err : = decodeRef(elems, cachegen)
			if err != nil{
				return n, wrapError(err, fmt.Sprintf("[%d]", i))
			}
				n.Children[i], elems = cld, rest
	}
	val, _, err : = rlp.SplitString(elems)
		if err != nil{
			return n, err
		}
			if len(val) > 0 {
				n.Children[16] = append(valueNode{}, val...)
			}
	return n, nil
}

