# SSA IR и байткод

## 1. Цели
- Платформонезависимое представление, удобное для оптимизаций и JIT.
- Строгая SSA: каждое значение присваивается один раз, `phi` на стыке блоков.
- Текстовый и бинарный формат для удобного дебага.

## 2. Структура IR

### Объекты верхнего уровня
```
Module {
	data   : ConstantPool,
	funcs  : List<Function>,
	types  : TypeTable,
}

Function {
	name      : string,
	params    : [Type],
	ret       : Type,
	blocks    : [BasicBlock],
	metadata  : {key: value},
}

BasicBlock {
	label     : string,
	preds     : [BlockId],
	instrs    : [Instruction],
	terminator: Terminator,
}
```

### Инструкции

| Категория | Инструкции |
| --- | --- |
| Арифметика | `add`, `sub`, `mul`, `div`, `mod` |
| Сравнения | `icmp` (`eq`, `ne`, `lt`, `le`, `gt`, `ge`), `fcmp` |
| Память | `alloca`, `load`, `store`, `spill`, `stackslot` |
| Управление | `br`, `condbr`, `ret`, `switch`, `phi` |
| Вызовы | `call`, `call_indirect`, `intrinsic` |
| Heap | `heap_alloc`, `heap_free` (служебно для GC), `gc_write_barrier` |

Каждая инструкция имеет вид:
```
%3 = add %1, %2        ; присваивание новой SSA переменной
br %cond, ^then, ^else ; терминатор
```

### Пример
```
func @factorial(%n: int) -> int {
^entry:
	%cond = icmp le %n, 1
	condbr %cond, ^base, ^recurse
^base:
	ret 1
^recurse:
	%n1 = sub %n, 1
	%tmp = call @factorial(%n1)
	%res = mul %n, %tmp
	ret %res
}
```

## 3. Байткод `ImpulseBC`

- Формат little-endian.
- Заголовок: `magic(0x494D504C)`, версия, таблица смещений.
- Таблица констант: литералы (int/float/string), ссылки на типы.
- Таблица функций: metadata, скомпилированные инструкции.
- Инструкции фиксированной ширины 32 бита: [opcode(8)][dst/reg(8)][arg0(8)][arg1(8)] — при необходимости extra word.

Пример инструкции:
```
ADD r1, r2, r3   ; r1 = r2 + r3
JMP label_id
CALL func_id, argc
```

## 4. Верификатор IR
- Проверяет, что каждая SSA-переменная определена ровно один раз и используется после определения.
- CFG должен быть связанным, все пути из входа доходят до терминатора.
- Типы совместимы: `add` применим только к числовым типам, `phi` — ко значениям одинакового типа.
- Для GC-инструкций проверяется наличие safepoint метаданных.

## 5. Пайплайн оптимизаций
- **Level 0**: CFG cleanup, DCE, простые константы.
- **Level 1**: propagation, common-subexpression elimination, loop-simplify, sparse conditional constant propagation.
- **Level 2**: inline, LICM, strength reduction, подготовка профиля для JIT.
- После Level 2 IR преобразуется в байткод или в машинный код (JIT) напрямую.

## 6. Mapping IR ↔ Bytecode
- Каждому SSA-регистру сопоставляется виртуальный регистр байткода.
- Phi-разрешение выполняется вставкой копий в предшественниках.
- Терминаторы `condbr` → `JMP/JMP_IF` с адресами блоков.

## 7. TODO
- Полный список опкодов и их бинарное представление.
- Формат metadata для профилирования.
- Спецификация debug-информации.
