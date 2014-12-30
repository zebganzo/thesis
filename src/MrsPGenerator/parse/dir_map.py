import numpy as np
import os
import re

from collections import defaultdict

class DirMapNode(object):
    def __init__(self):
        self.children = defaultdict(DirMapNode)
        self.values = []

    def heir(self, generation=1):
        def heir2(node, generation):
            if not generation:
                return node
            elif not node.children:
                return None
            else:
                next_heir = node.children.values()[0]
                return next_heir.heir(generation - 1)
        return heir2(self, generation)

    def leafs(self, path=[], offset=0):
        path = list(path)
        check_node = self.heir(offset)
        if check_node and check_node.children:
            for child_name, child_node in self.children.iteritems():
                path += [child_name]
                for leaf in child_node.leafs(path, offset):
                    yield leaf
                path.pop()
        else:
            yield (path, self)

class DirMap(object):

    def __init__(self):
        self.root = DirMapNode()
        self.values  = []

    def add_values(self, path, values):
        node = self.root
        for p in path:
            node = node.children[p]
        node.values += values

    def remove_childless(self):
        def remove_childless2(node):
            for key, child in node.children.items():
                remove_childless2(child)
                if not (child.children or child.values):
                    node.children.pop(key)

            if len(node.values) == 1:
                node.values = []

        remove_childless2(self.root)

    def is_empty(self):
        return not len(self.root.children)

    def write(self, out_dir):
        def write2(path, node):
            out_path = "/".join(path)
            if node.values:
                # Leaf
                with open("/".join(path), "w") as f:
                    arr = [",".join([str(b) for b in n]) for n in node.values]
                    arr = sorted(arr, key=lambda x: x[0])
                    f.write("\n".join(arr) + "\n")
            elif not os.path.isdir(out_path):
                os.mkdir(out_path)

            for (key, child) in node.children.iteritems():
                path.append(key)
                write2(path, child)
                path.pop()

        write2([out_dir], self.root)

    def leafs(self, offset=0):
        for leaf in self.root.leafs([], offset):
            yield leaf

    @staticmethod
    def read(in_dir):
        dir_map = DirMap()
        if not os.path.exists(in_dir):
            raise ValueError("Can't load from nonexistent path : %s" % in_dir)

        def read2(path):
            if os.path.isdir(path):
                map(lambda x : read2(path+"/"+x), os.listdir(path))
            else:
                if not re.match(r'.*\.csv', path):
                    return

                with open(path, 'rb') as f:
                    try:
                        data = np.loadtxt(f, delimiter=",")
                    except Exception as e:
                        raise IOError("Cannot load '%s': %s" % (path, e.message))

                # Convert to tuples of ints if possible, else floats
                values = [map(lambda a:a if a%1 else int(a), t) for t in data]
                values = map(tuple, values)

                stripped = path if path.find(in_dir) else path[len(in_dir):]
                path_arr = stripped.split("/")
                path_arr = filter(lambda x: x != '', path_arr)

                dir_map.add_values(path_arr, values)

        read2(in_dir)

        return dir_map

    def __str__(self):
        def str2(node, level):
            header = "  " * level
            ret = ""
            if not node.children:
                return "%s%s\n" % (header, str(node.values) if node.values else "")
            for key,child in node.children.iteritems():
                ret += "%s/%s\n" % (header, key)
                ret += str2(child, level + 1)
            return ret
        return str2(self.root, 1)
