# SSA IR и байткод

## 1. Цели
- Платформонезависимое представление, удобное для оптимизаций и JIT.
- Строгая SSA: каждое значение присваивается один раз, `phi` на стыке блоков.
- Текстовый и бинарный формат для удобного дебага.

### Bootstrap-представление (MVP)
Пока SSA и байткод в разработке, фронтенд уже умеет опускать AST в упрощённый IR-модуль:

- `Module` хранит путь (`module foo::bar;`), список binding'ов, функций, структур и интерфейсов, а также помечает, какие сущности объявлены с `export`.
- Binding описывается storage-классом (`let/const/var`), типом и текстом инициализатора.
- Функции содержат сигнатуры и `blocks` — список базовых блоков с инструкциями. Пока доступно два вида инструкций: `comment` (одержит сырой snippet) и `return` (опционально с операндом).
- Структуры содержат имена полей и их типы.

`impulsec --emit-ir` печатает это представление и служит связкой между фронтендом и дальнейшими IR/VM слоями. Текстовый вывод уже включает интерфейсы, чтобы ранние эксперименты с типовыми классами не терялись по пути к IR. По мере появления настоящего SSA эти структуры станут источником для генерации блоков/инструкций.

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

## 7. Таблица опкодов

| Opcode | Mnemonic | Категория | Описание |
| --- | --- | --- | --- |
| `0x00` | `NOP` | misc | нет операции (для выравнивания) |
| `0x01` | `LOADK` | константы | `dst = const[id]` |
| `0x02` | `MOVE` | копия | `dst = src` |
| `0x10` | `ADD` | арифметика | `dst = a + b` |
| `0x11` | `SUB` | | `dst = a - b` |
| `0x12` | `MUL` | | `dst = a * b` |
| `0x13` | `DIV` | | `dst = a / b` |
| `0x14` | `MOD` | | `dst = a % b` |
| `0x18` | `NEG` | | `dst = -a` |
| `0x20` | `ICMP` | сравнение | `dst = (a OP b)` (OP кодируется во флагах) |
| `0x21` | `FCMP` | | |
| `0x30` | `JMP` | управление | безусловный переход |
| `0x31` | `JZ` | | переход, если `src == 0` |
| `0x32` | `JNZ` | | переход, если `src != 0` |
| `0x33` | `RET` | | возврат `src` |
| `0x40` | `CALL` | вызовы | прямой вызов функции |
| `0x41` | `CALLI` | | косвенный (через адрес) |
| `0x42` | `TAILCALL` | | хвостовой вызов |
| `0x50` | `ALLOCA` | стек | выделение слота |
| `0x51` | `LOAD` | память | `dst = *(addr + offset)` |
| `0x52` | `STORE` | | `*(addr + offset) = src` |
| `0x60` | `HNEW` | heap | аллокатор heap |
| `0x61` | `HBAR` | | write barrier |

Расширение таблицы допускается, но сохранение обратной совместимости обязательно.

## 8. Metadata и debug info
- **Profile metadata**: в `Function.metadata` ключ `profile.hotness` (значения `cold`, `warm`, `hot`), `profile.exec_count`, `profile.edge_freq`. Он загружается рантаймом и используется JIT для выбора порога компиляции.
- **GC safepoints**: блоки содержат список инструкций с флагом `safepoint=true`, где VM может остановиться для GC. Для каждой точки хранится `live_set` регистров.
- **Debug info**: на уровне IR — `loc = {file, line, column}` для каждой инструкции. При сериализации в байткод создаётся отдельная секция `DBG` с map `ip -> loc`.

## 9. Debug формат
- Секция `DBG` содержит:
	- `u32 entry_count`
	- записи вида `{u32 ip_offset; u32 file_id; u32 line; u32 column;}`
- Таблица файлов (`file_id -> path`) хранится в общей секции `STR`. CLI может отображать стек-трейсы и исходники.
