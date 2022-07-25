# Eth
Project:research report on MPT

MPT是以太坊中使用很广泛的数据结构，用来管理以太坊账户的状态、状态的变更、交易和收据等。

New()函数接受一个根hash值和一个Database参数，初始化一个trie。

resolveHash()方法通过hash解析出node的RLP值，它在Trie的序列化和反序列化过程中多次使用New()新建一个树时用于获取根节点、tryGet()通过给定的key去找对应的value,从根节点递归想想查询，当查到了hashNode的时候将该hashNode对应的Node找出来，基于该Node进一步去查询，直到找到valueNode、insert()插入节点遍历到hashNode时，说明该节点还没有被载入内存，此时调用resolveHash取出该Node，进一步执行insert()、delete()删除节点遍历到hashNode时，用途与insert()类似。

trie.Commit()方法进行序列化并得到一个hash值(root)，它的主要流程通过hasher.go中的hash()方法来实现。

根hash是通过遍历shortNode和fullNode，将其中的子节点rlp编码值转换为hash值，构成了一个全部由hash组成的树；同时将每个节点的hash值存入每个节点的node.flag.hash中，构成了一个包含了hash的副本Trie，并传给cache。

hash()方法将node折叠为hash，从root节点开始，调用hashChildren()将子节点折叠成hashNode，替换到子节点原来所在父节点中的位置，直到所有的子节点都变为hash值；上面的过程是一个递归调用，通过hash()调用hashChildren()，hashChildren()判断如果不是valueNode则递归调用hash()，这样使得整个折叠的过程从valueNode开始，最后终结于root。调用store()方法对这个全部由子节点hash组成的node进行RLP编码，并进行哈希计算，得到该节点的hashNode；同时以hashNode为key，以本node的RLP编码的值为value存入数据库。

decodeNode()方法，根据rlp的list的长度来判断这个编码到底属于什么节点，如果是2个字段那么就是shortNode节点，如果是17个字段，那么就是fullNode，然后分别调用各自的解析函数。

decodeShort()方法，通过key是否有终结符号来判断到底是叶子节点还是扩展节点。如果有终结符那么就是叶子结点，通过SplitString方法解析出来val然后生成一个shortNode。 不过没有终结符，那么说明是扩展节点， 通过decodeRef来解析剩下的节点，然后生成一个shortNode。

decodeRef()方法根据数据类型进行解析，如果类型是list，那么有可能是内容<32的值，那么调用decodeNode进行解析。如果是空节点，那么返回空，如果是hash值，那么构造一个hashNode进行返回。
