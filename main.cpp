#include "parser.cpp"

struct Expression
{
    X86Gp reg;
    Type *type;
    bool is_method;
    X86Gp object;
};

typedef uint64_t (*Function)();

void jit_statement(X86Compiler &a, ProgramData *statement, JitState &state);
Expression jit_expression(X86Compiler &a, ProgramData *expression, JitState &state)
{
    if (expression->type == TYPE_INTEGER)
    {
        X86Gp v_reg = a.newGpq();
        X86Mem c0 = a.newInt64Const(kConstScopeLocal, expression->value.integer);
        a.mov(v_reg, c0);

        return {v_reg, nullptr};
    }

    if (expression->type == TYPE_FUNCTION_DEF)
    {
        auto &vec = (*expression->value.children);

        CCFunc* func = a.newFunc(FuncSignature0<uint64_t>(CallConv::kIdHost));
        state.remainders.push_back({func, std::move(vec[0])});

        X86Gp v_reg = a.newGpq();
        a.lea(v_reg, x86::ptr(func->getLabel()));

        return {v_reg, get_return_type(nullptr), false};
    }

    if (expression->type == TYPE_INDEX)
    {
        auto &vec = (*expression->value.children);
        Expression exp = jit_expression(a, vec[0].get(), state);

        Type *t = exp.type;
        char *property = vec[1]->value.str;

        if (t == nullptr)
            throw "TYPE WAS NULL";

        if (t->function_lookup.find(property) == t->function_lookup.end())
            throw ("TYPE DOES NOT HAVE " + std::string(property)).c_str();

        int function_number = t->function_lookup[property];

        X86Mem c0 = a.newInt64Const(kConstScopeLocal, ~(NUM_BIT));

        X86Gp copy = a.newGpq();
        a.mov(copy, exp.reg);

        a.and_(exp.reg, c0);
        X86Mem m = x86::ptr(exp.reg, offsetof(Obj, funcs));
        X86Gp obj = a.newGpq("Index");
        m.setSize(sizeof(generic_fp**));
        a.mov(obj, m);
        a.mov(obj, x86::ptr(obj));
        a.mov(obj, x86::ptr(obj, function_number*sizeof(generic_fp)));

        return {obj, t->functions.type[function_number], t->functions.is_method[function_number], copy};
    }

    if (expression->type == TYPE_IDENTIFIER)
    {
        if (state.vars.find(expression->value.str) != state.vars.end())
        {
            StackVar var = state.vars[expression->value.str];

            X86Gp v_reg = a.newGpq();
            state.mem.setSize(8);
            state.mem.setOffset(var.stack_offset);  

            a.mov(v_reg, state.mem);

            return {v_reg, var.type, false};
        }
        else if (state.globals.find(expression->value.str) != state.globals.end())
        {
            GlobalVar var = state.globals[expression->value.str];

            X86Gp v_reg = a.newGpq();
            X86Mem c0 = a.newInt64Const(kConstScopeGlobal, var.value);
            a.mov(v_reg, c0);

            return {v_reg, var.type, false};
        }
        else
        {
            throw ("USING " + std::string(expression->value.str) + " BEFORE DEFINED").c_str();
        }
    }

    if (expression->type == TYPE_FUNCTION)
    {
        auto &vec = (*expression->value.children);

        Expression exp = jit_expression(a, vec[0].get(), state);
        X86Gp func = exp.reg;
        Type *type = exp.type;

        X86Gp args[vec.size()];
        int arg_num = 0;
        for (auto it = ++vec.begin(); it != vec.end(); ++it)
        {
            args[arg_num++] = jit_expression(a, (*it).get(), state).reg;
        }

        int off = exp.is_method ? 1 : 0;

        X86Gp ret = a.newGpq();
        CCFuncCall *node;
        switch (vec.size() + off)
        {
            case 0:
                node = a.call(func, FuncSignature0<uint64_t>(CallConv::kIdHostCDecl));
                break;
            case 1:
                node = a.call(func, FuncSignature1<uint64_t, uint64_t>(CallConv::kIdHostCDecl));
                break;
            case 2:
                node = a.call(func, FuncSignature2<uint64_t, uint64_t, uint64_t>(CallConv::kIdHostCDecl));
                break;
            case 3:
                node = a.call(func, FuncSignature3<uint64_t, uint64_t, uint64_t, uint64_t>(CallConv::kIdHostCDecl));
                break;
            case 4:
                node = a.call(func, FuncSignature4<uint64_t, uint64_t, uint64_t, uint64_t, uint64_t>(CallConv::kIdHostCDecl));
                break;
        }

        if (exp.is_method)
        {
            node->setArg(0, exp.object);
        }

        for (int i = 0; i < vec.size(); i++)
        {
            node->setArg(i + off, args[i]);
        }
        node->setRet(0, ret);

        return {ret, type->return_type};
    }

    if (expression->type == TYPE_SUB)
    {
        X86Gp reg_a = jit_expression(a, (*expression->value.children)[0].get(), state).reg;
        X86Gp reg_b = jit_expression(a, (*expression->value.children)[1].get(), state).reg;

        a.sub(reg_a, reg_b);

        return {reg_a, nullptr};
    }

    if (expression->type == TYPE_ADD)
    {
        X86Gp reg_a = jit_expression(a, (*expression->value.children)[0].get(), state).reg;
        X86Gp reg_b = jit_expression(a, (*expression->value.children)[1].get(), state).reg;

        a.add(reg_a, reg_b);

        return {reg_a, nullptr};
    }

    if (expression->type == TYPE_MULT)
    {
        X86Gp reg_a = jit_expression(a, (*expression->value.children)[0].get(), state).reg;
        X86Gp reg_b = jit_expression(a, (*expression->value.children)[1].get(), state).reg;

        a.imul(reg_a, reg_b);

        return {reg_a, nullptr};
    }

    if (expression->type == TYPE_DIV)
    {
        X86Gp reg_a = jit_expression(a, (*expression->value.children)[0].get(), state).reg;
        X86Gp reg_b = jit_expression(a, (*expression->value.children)[1].get(), state).reg;
        X86Gp reg_c = a.newGpq();

        a.mov(reg_c, 0);
        a.idiv(reg_c, reg_a, reg_b);

        return {reg_a, nullptr};
    }

    if (expression->type == TYPE_NOTEQUALITY)
    {
        X86Gp reg_a = jit_expression(a, (*expression->value.children)[0].get(), state).reg;
        X86Gp reg_b = jit_expression(a, (*expression->value.children)[1].get(), state).reg;

        a.sub(reg_a, reg_b);
        a.setne(reg_a);

        return {reg_a, nullptr};
    }

    if (expression->type == TYPE_EQUALITY)
    {
        X86Gp reg_a = jit_expression(a, (*expression->value.children)[0].get(), state).reg;
        X86Gp reg_b = jit_expression(a, (*expression->value.children)[1].get(), state).reg;

        a.sub(reg_a, reg_b);
        a.sete(reg_a);

        return {reg_a, nullptr};
    }

    if (expression->type == TYPE_LT)
    {
        X86Gp reg_a = jit_expression(a, (*expression->value.children)[0].get(), state).reg;
        X86Gp reg_b = jit_expression(a, (*expression->value.children)[1].get(), state).reg;

        a.sub(reg_a, reg_b);
        a.setl(reg_a);

        return {reg_a, nullptr};
    }

    throw strdup(("UNKOWN EXPRESSION " + std::to_string(expression->type)).c_str());
}

void jit_statement(X86Compiler &a, ProgramData *statement, JitState &state)
{
    if (statement->type == TYPE_ASSIGNMENT)
    {
        Expression exp = jit_expression(a, (*statement->value.children)[1].get(), state);

        auto var = (*statement->value.children)[0]->value.str;
        if (state.vars.find(var) == state.vars.end())
        {
            state.vars[var] = {exp.type, state.offset};
            state.offset += 8;
        }

        state.mem.setSize(8);
        state.mem.setOffset(state.vars[var].stack_offset);

        a.mov(state.mem, exp.reg);

        return;
    }

    if (statement->type == TYPE_RETURN)
    {
        Expression exp = jit_expression(a, (*statement->value.children)[0].get(), state);
        a.ret(exp.reg);

        return;
    }

    if (statement->type == TYPE_BLOCK)
    {
        auto &vec = (*statement->value.children);
        for (auto it = vec.begin(); it != vec.end(); ++it)
        {
            jit_statement(a, (*it).get(), state);
        }

        return;
    }

    if (statement->type == TYPE_IF)
    {
        Label L1 = a.newLabel();
        Label L2 = a.newLabel();

        X86Gp reg = jit_expression(a, (*statement->value.children)[0].get(), state).reg;

        a.cmp(reg, 0);
        a.je(L1);

        jit_statement(a, (*statement->value.children)[1].get(), state);
        a.jmp(L2);
        a.bind(L1);

        if ((*statement->value.children)[2] != nullptr)
            jit_statement(a, (*statement->value.children)[2].get(), state);        

        a.bind(L2);

        return;
    }

    if (statement->type == TYPE_WHILE)
    {

        Label L1 = a.newLabel();
        Label L2 = a.newLabel();

        a.bind(L1);
        X86Gp reg = jit_expression(a, (*statement->value.children)[0].get(), state).reg;

        a.cmp(reg, 0);
        a.je(L2);

        jit_statement(a, (*statement->value.children)[1].get(), state);
        a.jmp(L1);

        a.bind(L2);

        return;
    }

    jit_expression(a, statement, state);
}

typedef int (*SumFunc)();

int main(int argc, char const *argv[])
{
    JitRuntime rt;
    FileLogger logger(stdout); 

    CodeHolder code;
    code.init(CodeInfo(ArchInfo::kTypeX64));
    code.setLogger(&logger);

    X86Compiler a(&code);
    a.addFunc(FuncSignature0<void>()); 

    ParserResult res;

    std::ifstream t(argv[1]);
    std::string program((std::istreambuf_iterator<char>(t)),
                     std::istreambuf_iterator<char>());

    JitState s = { 0, {}, {}, {}, a.newStack(256, 8), a.newIntPtr("i"), {} };

    register_types(s);
    // register_functions(s.funcs);

    while (!program.empty()) 
    {
        res = statement(program);
        if (!res.success) 
        {
            printf("ERROR IN PARSING\n");
            return 0;
        }

        try {
            jit_statement(a, res.data.get(), s);
        } catch (char const* err) {
            printf("%s\n", err);
            return 0;
        }
        program = res.remainder;
    }

    if (a.isInErrorState())
        printf("ERROR: %s\n", DebugUtils::errorAsString(a.getLastError()));

    a.endFunc();                           // End of the function body.
    
    while (!s.remainders.empty())
    {
        s = { 0, {}, {}, s.globals, a.newStack(256, 8), a.newIntPtr("i"), std::move(s.remainders) };

        FunctionRemainder frem = std::move(s.remainders[0]);
        s.remainders.erase(s.remainders.begin());

        a.addFunc(frem.func);

        try {
            jit_statement(a, frem.data.get(), s);
        } catch (char const* err) {
            printf("%s\n", err);
            return 0;
        }

        X86Gp r = a.newGpq();

        a.mov(r, 0);
        a.ret(r);

        a.endFunc();
    }

    a.finalize();

    SumFunc fn;
    Error err = rt.add(&fn, &code);

    if (err) {
        printf("wack\n");
        return 1;
    }

    printf("\nRUNNING\n\n");

    fn();              // Execute the generated code.

    return 0;
}
