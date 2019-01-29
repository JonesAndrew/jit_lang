#include <string>
#include <vector>
#include <unordered_map>
#include <cassert>

#include <asmjit/asmjit.h>

using namespace asmjit;

struct Type;
struct ProgramData;

struct StackVar
{
    Type *type;
    int stack_offset;
};

struct GlobalVar
{
    Type *type;
    uint64_t value;
};

struct FunctionRemainder
{
    CCFunc* func;
    std::unique_ptr<ProgramData> data;
};

struct JitState
{
    int offset;
    std::unordered_map<std::string, StackVar> vars;
    std::unordered_map<std::string, Imm> funcs;
    std::unordered_map<std::string, GlobalVar> globals;
    X86Mem mem;
    X86Gp stack_offset;
    std::vector<FunctionRemainder> remainders;
};

#define SIGN_BIT ((uint64_t)1 << 63)
#define NUM_BIT ((uint64_t)1 << 62)

typedef union
{
    int64_t bits64;
    uint64_t ubits64;
    double num;
} DoubleBits;

typedef uint64_t (*generic_fp)(void);
typedef uint64_t (*func1)(uint64_t a);
typedef uint64_t (*func2)(uint64_t a, uint64_t b);
typedef uint64_t (*func3)(uint64_t a, uint64_t b, uint64_t c);
typedef uint64_t (*func4)(uint64_t a, uint64_t b, uint64_t c, uint64_t d);

struct FunctionTable
{
    bool *is_method;
    generic_fp *func;
    Type **type;

    int count;
    int capacity;
};

struct Type
{
    int type_number;
    std::string name;
    FunctionTable functions;
    Type *return_type;

    std::unordered_map<std::string, int> function_lookup;
};

void add_function(Type *t, std::string name, bool is_method, generic_fp func, Type *function_type)
{
    if (t->functions.count == t->functions.capacity)
    {
        t->functions.capacity *= 2;

        bool *is_method = new bool[t->functions.capacity];
        generic_fp *func = new generic_fp[t->functions.capacity];
        Type **type = new Type*[t->functions.capacity];

        for (int i=0; i < t->functions.count; i++)
        {
            is_method[i] = t->functions.is_method[i];
            func[i] = t->functions.func[i];
            type[i] = t->functions.type[i];
        }

        delete t->functions.is_method;
        delete t->functions.func;
        delete t->functions.type;

        t->functions.is_method = is_method;
        t->functions.func = func;
        t->functions.type = type;
    }

    t->functions.is_method[t->functions.count] = is_method;
    t->functions.func[t->functions.count] = func;
    t->functions.type[t->functions.count] = function_type;

    t->function_lookup[name] = t->functions.count++;
}

int type_count = 0;
std::unordered_map<std::string, Type *> types;

const int list_type_number = 0;
Type *list_type;

Type *function_type;

Type *register_type(std::string name)
{
    Type *t = new Type({type_count++, name, {} });
    types[name] = t;

    t->functions.count = 0;
    t->functions.capacity = 8;

    t->functions.is_method = new bool[t->functions.capacity];
    t->functions.func = new generic_fp[t->functions.capacity];
    t->functions.type = new Type*[t->functions.capacity];

    return t;
}

struct Obj
{
    int type;
    generic_fp **funcs;
    // uint64_t *vars;
};

struct List : public Obj
{
    uint64_t *elements;
    unsigned int capacity;
    unsigned int size;
};

inline int64_t valueToNum(uint64_t value)
{
    return (int64_t)(value);
}

inline Obj *valueToObj(uint64_t value)
{
    return ((Obj*)(uintptr_t)((value) & ~(NUM_BIT)));
}

inline uint64_t objToValue(Obj *obj)
{
    return (uint64_t)(NUM_BIT | (uint64_t)(uintptr_t)(obj)); 
};

inline generic_fp valueToFunc(uint64_t value)
{
    return ((generic_fp)(uintptr_t)(value));
}

inline uint64_t funcToValue(generic_fp func)
{
    return (uint64_t)((uintptr_t)(func)); 
};

inline bool isNum(uint64_t value)
{
    return (value & NUM_BIT) == 0;
}

inline bool isObjType(uint64_t value, int type)
{
    return valueToObj(value)->type == type;
}


Obj *setup_object(Obj *o, Type *t)
{
    o->type = t->type_number;
    o->funcs = &t->functions.func;
    // o->vars = new uint64_t[8];

    return o;
}

uint64_t make_list()
{
    List *l = (List *)setup_object(new List(), list_type);
    
    l->capacity = 8;
    l->size = 0;
    l->elements = new uint64_t[l->capacity];

    return objToValue(l);
}

uint64_t list_add_element(uint64_t list, uint64_t elm)
{
    List *l = (List *)valueToObj(list);
    if (l->size >= l->capacity)
    {
        l->capacity = l->capacity * 2;
        uint64_t *elems = new uint64_t[l->capacity];

        for (int i = 0; i < l->size; i++)
        {
            elems[i] = l->elements[i];
        }

        delete l->elements;
        l->elements = elems;
    }

    l->elements[l->size++] = elm;
    return list;
}

uint64_t list_count(uint64_t list)
{
    List *l = (List *)valueToObj(list);
    return l->size;
}

uint64_t print(uint64_t a)
{
    if (isNum(a))
        printf("%lli\n", valueToNum(a));
    else if (isObjType(a, list_type_number))
    {
        List *l = (List*)valueToObj(a);

        printf("[\n");
        for (int i = 0; i < l->size; i++)
        {
            print(l->elements[i]);
        }
        printf("]\n");
    }

    return 0;
}

struct JitState;


std::unordered_map<Type *, Type *> return_types;
Type *get_return_type(Type *t)
{
    if (return_types.find(t) != return_types.end())
        return return_types[t];

    Type *ret = register_type("");
    ret->return_type = t;
    return_types[t] = ret;
    return ret;
}

void register_types(JitState &s)
{
    list_type = register_type("list");
    assert(list_type->type_number == list_type_number);

    add_function(list_type, "add", true, (generic_fp)list_add_element, get_return_type(list_type));
    add_function(list_type, "count", true, (generic_fp)list_count, get_return_type(nullptr));

    s.globals["make_list"] = {get_return_type(list_type), funcToValue((generic_fp)make_list)};
    s.globals["print"] = {get_return_type(nullptr), funcToValue((generic_fp)print)};
}
