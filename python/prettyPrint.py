
def matrix(mat, title="", tabs=""):
    '''prints a pretty matrix'''
    print(f'{tabs}{title}')    
    for row in range(len(mat)):
        print(f'{tabs}row {len(mat)-row-1}: [', end='')
        for col in range(len(mat[row])):
            space = " " if mat[-row-1][col] < 10 else ""
            space += " " if mat[-row-1][col] >= 0 else ""
            print(f"{space}{mat[-row-1][col]:.2f}", end=', ')
        print(']')

def net(net, design):
    for node_index in net:
        coord(design.coords[node_index])
    print()

def nets(design):
    print()
    for i in range(len(design.nets)):
        print(f'Net {i}: {design.nets[i]}:', end='')
        net(design.nets[i], design)


def coord(coord):
        print(f' ({coord.row:.2f}, {coord.col:.2f}) ', end='')