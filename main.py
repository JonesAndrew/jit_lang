def compile():
    statement(ins, [])


def statement(ins, program):
    result = assignment(ins, program)
    if result[0]:
        return result

def assignment(ins, program):
    result = identifier(ins, program)
    if not result[0]:
        return (False)




print(compile("x = 5"))