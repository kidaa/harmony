
/*
 * Copyright 2005 The Apache Software Foundation or its licensors,
 * as applicable.
 * 
 *  Licensed under the Apache License, Version 2.0 (the "License");
 *  you may not use this file except in compliance with the License.
 *  You may obtain a copy of the License at
 * 
 *     http://www.apache.org/licenses/LICENSE-2.0
 * 
 *  Unless required by applicable law or agreed to in writing, software
 *  distributed under the License is distributed on an "AS IS" BASIS,
 *  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *  See the License for the specific language governing permissions and
 *  limitations under the License.
 *
 * $Id$
 */

#include "libjc.h"

/* Internal functions */
static jint	_jc_interp(_jc_env *env, _jc_method *const method);
static int	_jc_lookup_compare(const void *v1, const void *v2);
static void	_jc_vinterp(_jc_env *env, va_list args);
static void	_jc_vinterp_native(_jc_env *env, va_list args);

/*
 * Macros used in _jc_interp()
 */
#ifndef NDEBUG

#define JUMP(_pc)							\
    do {								\
	pc = (_pc);							\
	_JC_ASSERT(sp >= locals + code->max_locals);			\
	_JC_ASSERT(sp <= locals + code->max_locals + code->max_stack);	\
	_JC_ASSERT(pc >= 0 && pc < code->num_insns);			\
	_JC_ASSERT(actions[code->opcodes[pc]] != NULL);			\
	_JC_ASSERT(ticker > 0);						\
	if (--ticker == 0)						\
		goto periodic_check;					\
	goto *actions[code->opcodes[pc]];				\
    } while (0)

#define NEXT()		JUMP(pc + 1)

#else	/* !NDEBUG */

#define JUMP(_pc)							\
    do {								\
	pc = (_pc);							\
	if (--ticker == 0)						\
		goto periodic_check;					\
	goto *actions[code->opcodes[pc]];				\
    } while (0)

#define NEXT()								\
    do {								\
	goto *actions[code->opcodes[++pc]];				\
    } while (0)

#endif	/* !NDEBUG */

#define STACKI(i)	(*(jint *)(sp + (i)))
#define STACKF(i)	(*(jfloat *)(sp + (i)))
#define STACKJ(i)	(*(jlong *)(sp + (i)))
#define STACKD(i)	(*(jdouble *)(sp + (i)))
#define STACKL(i)	(*(_jc_object **)(sp + (i)))
#define LOCALI(i)	(*(jint *)(sp + i))
#define LOCALF(i)	(*(jfloat *)(sp + i))
#define LOCALJ(i)	(*(jlong *)(sp + i))
#define LOCALD(i)	(*(jdouble *)(sp + i))
#define LOCALL(i)	(*(_jc_object **)(sp + i))
#define PUSHI(v)	do { STACKI(0) = (v); sp++; } while (0)
#define PUSHF(v)	do { STACKF(0) = (v); sp++; } while (0)
#define PUSHJ(v)	do { STACKJ(0) = (v); sp += 2; } while (0)
#define PUSHD(v)	do { STACKD(0) = (v); sp += 2; } while (0)
#define PUSHL(v)	do { STACKL(0) = (v); sp++; } while (0)
#define POP(i)		(sp -= (i))
#define POP2(i)		(sp -= 2 * (i))

#define INFO(f)		(code->info[pc].f)

#define ARRAYCHECK(array, i)						\
    do {								\
	_jc_array *const _array = (_jc_array *)(array);			\
	jint _i = (i);							\
									\
	if (_array == NULL)						\
		goto null_pointer_exception;				\
	if (_i < 0 || _i >= _array->length) {				\
		_jc_post_exception_msg(env,				\
		    _JC_ArrayIndexOutOfBoundsException, "%d", _i);	\
		goto exception;						\
	}								\
    } while (0)

#define PERIODIC_CHECK_TICKS	32

/*
 * Java interpreter. The "args" must contain two elements for long/double.
 *
 * If successful, return value is stored in env->retval and JNI_OK returned.
 * Otherwise, an exception is posted and JNI_ERR is returned.
 */
static jint
_jc_interp(_jc_env *const env, _jc_method *const method)
{
#define ACTION(name)  [_JC_ ## name]= &&do_ ## name
#define TARGET(name)  do_ ## name:   asm ("/***** " #name " *****/");
	static const void *const actions[0x100] = {
		ACTION(aaload),
		ACTION(aastore),
		ACTION(aload),
		ACTION(anewarray),
		ACTION(areturn),
		ACTION(arraylength),
		ACTION(astore),
		ACTION(athrow),
		ACTION(baload),
		ACTION(bastore),
		ACTION(caload),
		ACTION(castore),
		ACTION(checkcast),
		ACTION(d2f),
		ACTION(d2i),
		ACTION(d2l),
		ACTION(dadd),
		ACTION(daload),
		ACTION(dastore),
		ACTION(dcmpg),
		ACTION(dcmpl),
		ACTION(ddiv),
		ACTION(dload),
		ACTION(dmul),
		ACTION(dneg),
		ACTION(drem),
		ACTION(dreturn),
		ACTION(dstore),
		ACTION(dsub),
		ACTION(dup),
		ACTION(dup_x1),
		ACTION(dup_x2),
		ACTION(dup2),
		ACTION(dup2_x1),
		ACTION(dup2_x2),
		ACTION(f2d),
		ACTION(f2i),
		ACTION(f2l),
		ACTION(fadd),
		ACTION(failure),
		ACTION(faload),
		ACTION(fastore),
		ACTION(fcmpg),
		ACTION(fcmpl),
		ACTION(fdiv),
		ACTION(fload),
		ACTION(fmul),
		ACTION(fneg),
		ACTION(frem),
		ACTION(freturn),
		ACTION(fstore),
		ACTION(fsub),
		ACTION(getfield_z),
		ACTION(getfield_b),
		ACTION(getfield_c),
		ACTION(getfield_s),
		ACTION(getfield_i),
		ACTION(getfield_j),
		ACTION(getfield_f),
		ACTION(getfield_d),
		ACTION(getfield_l),
		ACTION(getstatic),
		ACTION(getstatic_z),
		ACTION(getstatic_b),
		ACTION(getstatic_c),
		ACTION(getstatic_s),
		ACTION(getstatic_i),
		ACTION(getstatic_j),
		ACTION(getstatic_f),
		ACTION(getstatic_d),
		ACTION(getstatic_l),
		ACTION(goto),
		ACTION(i2b),
		ACTION(i2c),
		ACTION(i2d),
		ACTION(i2f),
		ACTION(i2l),
		ACTION(i2s),
		ACTION(iadd),
		ACTION(iaload),
		ACTION(iand),
		ACTION(iastore),
		ACTION(idiv),
		ACTION(if_acmpeq),
		ACTION(if_acmpne),
		ACTION(if_icmpeq),
		ACTION(if_icmpne),
		ACTION(if_icmplt),
		ACTION(if_icmpge),
		ACTION(if_icmpgt),
		ACTION(if_icmple),
		ACTION(ifeq),
		ACTION(ifne),
		ACTION(iflt),
		ACTION(ifge),
		ACTION(ifgt),
		ACTION(ifle),
		ACTION(ifnonnull),
		ACTION(ifnull),
		ACTION(iinc),
		ACTION(iload),
		ACTION(imul),
		ACTION(ineg),
		ACTION(instanceof),
		ACTION(invokeinterface),
		ACTION(invokespecial),
		ACTION(invokestatic),
		ACTION(invokestatic2),
		ACTION(invokevirtual),
		ACTION(ior),
		ACTION(irem),
		ACTION(ireturn),
		ACTION(ishl),
		ACTION(ishr),
		ACTION(istore),
		ACTION(isub),
		ACTION(iushr),
		ACTION(ixor),
		ACTION(jsr),
		ACTION(l2d),
		ACTION(l2f),
		ACTION(l2i),
		ACTION(ladd),
		ACTION(laload),
		ACTION(land),
		ACTION(lastore),
		ACTION(lcmp),
		ACTION(ldc),
		ACTION(ldc_string),
		ACTION(ldc2_w),
		ACTION(ldiv),
		ACTION(lload),
		ACTION(lmul),
		ACTION(lneg),
		ACTION(lookupswitch),
		ACTION(lor),
		ACTION(lrem),
		ACTION(lreturn),
		ACTION(lshl),
		ACTION(lshr),
		ACTION(lstore),
		ACTION(lsub),
		ACTION(lushr),
		ACTION(lxor),
		ACTION(monitorenter),
		ACTION(monitorexit),
		ACTION(multianewarray),
		ACTION(new),
		ACTION(newarray),
		ACTION(nop),
		ACTION(pop),
		ACTION(pop2),
		ACTION(putfield_z),
		ACTION(putfield_b),
		ACTION(putfield_c),
		ACTION(putfield_s),
		ACTION(putfield_i),
		ACTION(putfield_j),
		ACTION(putfield_f),
		ACTION(putfield_d),
		ACTION(putfield_l),
		ACTION(putstatic),
		ACTION(putstatic_z),
		ACTION(putstatic_b),
		ACTION(putstatic_c),
		ACTION(putstatic_s),
		ACTION(putstatic_i),
		ACTION(putstatic_j),
		ACTION(putstatic_f),
		ACTION(putstatic_d),
		ACTION(putstatic_l),
		ACTION(ret),
		ACTION(return),
		ACTION(saload),
		ACTION(sastore),
		ACTION(swap),
		ACTION(tableswitch),
	};
	_jc_method_code *const code = &method->code;
	int ticker = PERIODIC_CHECK_TICKS;
	_jc_java_stack stack_frame;
	_jc_word *const locals = env->sp;
	_jc_object *lock = NULL;
	_jc_word *sp;
	int pc = 0;

	/* Sanity check */
	_JC_ASSERT(env->sp != NULL);
	_JC_ASSERT(env->sp >= env->stack_data);
	_JC_ASSERT(env->sp <= env->stack_data + env->vm->java_stack_size);

	/* Stack overflow check for C stack */
#if _JC_DOWNWARD_STACK
	if ((char *)&env < env->stack_limit
	    && (env->in_vmex & (1 << _JC_StackOverflowError)) == 0)
#else
	if ((char *)&env > env->stack_limit
	    && (env->in_vmex & (1 << _JC_StackOverflowError)) == 0)
#endif
	{
		_jc_post_exception(env, _JC_StackOverflowError);
		return JNI_ERR;
	}

	/* Check Java stack overflow; release secret space during exception */
	if (env->sp + code->max_locals + code->max_stack
	    > env->stack_data_end) {
		if ((env->in_vmex & (1 << _JC_StackOverflowError)) == 0
		    || env->sp + code->max_locals + code->max_stack
		      > env->stack_data_end + _JC_JAVA_STACK_MARGIN) {
			_jc_post_exception(env, _JC_StackOverflowError);
			return JNI_ERR;
		}
	}

	/* Is method abstract? */
	if (_JC_ACC_TEST(method, ABSTRACT)) {
		_jc_post_exception_msg(env, _JC_AbstractMethodError,
		    "%s.%s%s", method->class->name, method->name,
		    method->signature);
		return JNI_ERR;
	}

	/* Sanity check */
	_JC_ASSERT(!_JC_ACC_TEST(method->class, INTERFACE)
	    || strcmp(method->name, "<clinit>") == 0);
	_JC_ASSERT(_JC_FLG_TEST(method->class, RESOLVED));
	_JC_ASSERT(!_JC_ACC_TEST(method, NATIVE));

	/* Push Java stack frame */
	memset(&stack_frame, 0, sizeof(stack_frame));
	stack_frame.next = env->java_stack;
	stack_frame.method = method;
	stack_frame.pcp = &pc;
	env->java_stack = &stack_frame;
	sp = locals + code->max_locals;
	env->sp = sp + code->max_stack;

	/* Sanity check */
	_JC_ASSERT(code->opcodes != NULL);
	_JC_ASSERT(code->num_insns > 0);

	/* Synchronize */
	if (_JC_ACC_TEST(method, SYNCHRONIZED)) {
		lock = _JC_ACC_TEST(method, STATIC) ?
		    method->class->instance : LOCALL(-code->max_locals);
		if (_jc_lock_object(env, lock) != JNI_OK) {
			lock = NULL;
			goto exception;
		}
	}

	/* Start */
	JUMP(0);

TARGET(aaload)
    {
	_jc_object_array *array;
	jint index;

	POP(2);
	array = (_jc_object_array *)STACKL(0);
	index = STACKI(1);
	ARRAYCHECK(array, index);
	_JC_ASSERT(_JC_LW_TEST(array->lockword, ARRAY)
	    && _JC_LW_EXTRACT(array->lockword, TYPE) == _JC_TYPE_REFERENCE);
	PUSHL(array->elems[~index]);
	NEXT();
    }
TARGET(aastore)
    {
	_jc_object_array *array;
	_jc_object *obj;
	jint index;

	POP(3);
	array = (_jc_object_array *)STACKL(0);
	index = STACKI(1);
	ARRAYCHECK(array, index);
	obj = STACKL(2);
	if (obj != NULL) {
		switch (_jc_assignable_from(env, obj->type,
		    array->type->u.array.element_type)) {
		case 1:
			break;
		case 0:
			_jc_post_exception_msg(env, _JC_ArrayStoreException,
			    "can't store object of type `%s' into array"
			    " of `%s'", obj->type->name,
			    array->type->u.array.element_type->name);
			/* FALLTHROUGH */
		case -1:
			goto exception;
		}
	}
	array->elems[~index] = obj;
	NEXT();
    }
TARGET(aload)
	PUSHL(LOCALL(INFO(local)));
	NEXT();
TARGET(anewarray)
    {
	_jc_array *array;
	jint length;

	POP(1);
	length = STACKI(0);
	if ((array = _jc_new_array(env, INFO(type), length)) == NULL)
		goto exception;
	PUSHL((_jc_object *)array);
	NEXT();
    }
TARGET(areturn)
	POP(1);
	env->retval.l = STACKL(0);
	goto done;
TARGET(arraylength)
    {
	_jc_array *array;

	POP(1);
	array = (_jc_array *)STACKL(0);
	if (array == NULL)
		goto null_pointer_exception;
	PUSHI(array->length);
	NEXT();
    }
TARGET(astore)
	POP(1);
	LOCALL(INFO(local)) = STACKL(0);
	NEXT();
TARGET(athrow)
	POP(1);
	if (STACKL(0) == NULL)
		goto null_pointer_exception;
	_jc_post_exception_object(env, STACKL(0));
	goto exception;
TARGET(baload)
    {
	_jc_array *array;
	jint index;
	int ptype;

	POP(2);
	array = (_jc_array *)STACKL(0);
	index = STACKI(1);
	ARRAYCHECK(array, index);
	ptype = _JC_LW_EXTRACT(array->lockword, TYPE);
	_JC_ASSERT(ptype == _JC_TYPE_BOOLEAN || ptype == _JC_TYPE_BYTE);
	if (ptype == _JC_TYPE_BOOLEAN)
		PUSHI(((_jc_boolean_array *)array)->elems[index]);
	else
		PUSHI(((_jc_byte_array *)array)->elems[index]);
	NEXT();
    }
TARGET(bastore)
    {
	_jc_array *array;
	jint index;
	int ptype;

	POP(3);
	array = (_jc_array *)STACKL(0);
	index = STACKI(1);
	ARRAYCHECK(array, index);
	ptype = _JC_LW_EXTRACT(array->lockword, TYPE);
	_JC_ASSERT(ptype == _JC_TYPE_BOOLEAN || ptype == _JC_TYPE_BYTE);
	if (ptype == _JC_TYPE_BOOLEAN)
		((_jc_boolean_array *)array)->elems[index] = STACKI(2) & 0x1;
	else
		((_jc_byte_array *)array)->elems[index] = STACKI(2) & 0xff;
	NEXT();
    }
TARGET(caload)
    {
	_jc_char_array *array;
	jint index;

	POP(2);
	array = (_jc_char_array *)STACKL(0);
	index = STACKI(1);
	ARRAYCHECK(array, index);
	PUSHI(array->elems[index]);
	NEXT();
    }
TARGET(castore)
    {
	_jc_char_array *array;
	jint index;

	POP(3);
	array = (_jc_char_array *)STACKL(0);
	index = STACKI(1);
	ARRAYCHECK(array, index);
	array->elems[index] = STACKI(2) & 0xffff;
	NEXT();
    }
TARGET(checkcast)
    {
	_jc_object *const obj = STACKL(-1);

	if (obj != NULL) {
		_jc_type *const type = INFO(type);

		switch (_jc_instance_of(env, obj, type)) {
		case 1:
			break;
		case 0:
			_jc_post_exception_msg(env, _JC_ClassCastException,
			    "can't cast `%s' to `%s'", obj->type->name,
			    type->name);
			/* FALLTHROUGH */
		case -1:
			goto exception;
		default:
			_JC_ASSERT(JNI_FALSE);
		}
	}
	NEXT();
    }
TARGET(d2f)
	POP2(1);
	PUSHF(STACKD(0));
	NEXT();
TARGET(d2i)
	POP2(1);
	PUSHI(_JC_CAST_FLT2INT(env, jdouble, jint, STACKD(0)));
	NEXT();
TARGET(d2l)
	POP2(1);
	PUSHJ(_JC_CAST_FLT2INT(env, jdouble, jlong, STACKD(0)));
	NEXT();
TARGET(dadd)
	POP2(2);
	PUSHD(STACKD(0) + STACKD(2));
	NEXT();
TARGET(daload)
    {
	_jc_double_array *array;
	jint index;

	POP(2);
	array = (_jc_double_array *)STACKL(0);
	index = STACKI(1);
	ARRAYCHECK(array, index);
	PUSHD(array->elems[index]);
	NEXT();
    }
TARGET(dastore)
    {
	_jc_double_array *array;
	jint index;

	POP(4);
	array = (_jc_double_array *)STACKL(0);
	index = STACKI(1);
	ARRAYCHECK(array, index);
	array->elems[index] = STACKD(2);
	NEXT();
    }
TARGET(dcmpg)
	POP2(2);
	PUSHI(_JC_DCMPG(STACKD(0), STACKD(2)));
	NEXT();
TARGET(dcmpl)
	POP2(2);
	PUSHI(_JC_DCMPL(STACKD(0), STACKD(2)));
	NEXT();
TARGET(ddiv)
	POP2(2);
	PUSHD(STACKD(0) / STACKD(2));
	NEXT();
TARGET(dload)
	PUSHD(LOCALD(INFO(local)));
	NEXT();
TARGET(dmul)
	POP2(2);
	PUSHD(STACKD(0) * STACKD(2));
	NEXT();
TARGET(dneg)
	POP2(1);
	PUSHD(-STACKD(0));
	NEXT();
TARGET(drem)
	POP2(2);
	PUSHD(fmod(STACKD(0), STACKD(2)));
	NEXT();
TARGET(dreturn)
	POP2(1);
	env->retval.d = STACKD(0);
	goto done;
TARGET(dstore)
	POP2(1);
	LOCALD(INFO(local)) = STACKD(0);
	NEXT();
TARGET(dsub)
	POP2(2);
	PUSHD(STACKD(0) - STACKD(2));
	NEXT();
TARGET(dup)
	STACKI(0) = STACKI(-1);
	POP(-1);
	NEXT();
TARGET(dup_x1)
	STACKI(0) = STACKI(-1);
	STACKI(-1) = STACKI(-2);
	STACKI(-2) = STACKI(0);
	POP(-1);
	NEXT();
TARGET(dup_x2)
	STACKI(0) = STACKI(-1);
	STACKI(-1) = STACKI(-2);
	STACKI(-2) = STACKI(-3);
	STACKI(-3) = STACKI(0);
	POP(-1);
	NEXT();
TARGET(dup2)
	STACKI(1) = STACKI(-1);
	STACKI(0) = STACKI(-2);
	POP(-2);
	NEXT();
TARGET(dup2_x1)
	STACKI(1) = STACKI(-1);
	STACKI(0) = STACKI(-2);
	STACKI(-1) = STACKI(-3);
	STACKI(-2) = STACKI(1);
	STACKI(-3) = STACKI(0);
	POP(-2);
	NEXT();
TARGET(dup2_x2)
	STACKI(1) = STACKI(-1);
	STACKI(0) = STACKI(-2);
	STACKI(-1) = STACKI(-3);
	STACKI(-2) = STACKI(-4);
	STACKI(-3) = STACKI(1);
	STACKI(-4) = STACKI(0);
	POP(-2);
	NEXT();
TARGET(f2d)
	POP(1);
	PUSHD(STACKF(0));
	NEXT();
TARGET(f2i)
	POP(1);
	PUSHI(_JC_CAST_FLT2INT(env, jfloat, jint, STACKF(0)));
	NEXT();
TARGET(f2l)
	POP(1);
	PUSHJ(_JC_CAST_FLT2INT(env, jfloat, jlong, STACKF(0)));
	NEXT();
TARGET(fadd)
	POP(2);
	PUSHF(STACKF(0) + STACKF(1));
	NEXT();
TARGET(faload)
    {
	_jc_float_array *array;
	jint index;

	POP(2);
	array = (_jc_float_array *)STACKL(0);
	index = STACKI(1);
	ARRAYCHECK(array, index);
	PUSHF(array->elems[index]);
	NEXT();
    }
TARGET(failure)
	_jc_post_exception_msg(env, _JC_InternalError, "failure opcode");
	goto exception;
TARGET(fastore)
    {
	_jc_float_array *array;
	jint index;

	POP(3);
	array = (_jc_float_array *)STACKL(0);
	index = STACKI(1);
	ARRAYCHECK(array, index);
	array->elems[index] = STACKF(2);
	NEXT();
    }
TARGET(fcmpg)
	POP(2);
	PUSHI(_JC_FCMPG(STACKF(0), STACKF(1)));
	NEXT();
TARGET(fcmpl)
	POP(2);
	PUSHI(_JC_FCMPL(STACKF(0), STACKF(1)));
	NEXT();
TARGET(fdiv)
	POP(2);
	PUSHF(STACKF(0) / STACKF(1));
	NEXT();
TARGET(fload)
	PUSHF(LOCALF(INFO(local)));
	NEXT();
TARGET(fmul)
	POP(2);
	PUSHF(STACKF(0) * STACKF(1));
	NEXT();
TARGET(fneg)
	POP(1);
	PUSHF(-STACKF(0));
	NEXT();
TARGET(frem)
	POP(2);
	PUSHF(fmod(STACKF(0), STACKF(1)));
	NEXT();
TARGET(freturn)
	POP(1);
	env->retval.f = STACKF(0);
	goto done;
TARGET(fstore)
	POP(1);
	LOCALF(INFO(local)) = STACKF(0);
	NEXT();
TARGET(fsub)
	POP(2);
	PUSHF(STACKF(0) - STACKF(1));
	NEXT();
TARGET(getfield_z)
    {
    	_jc_object *obj;

	POP(1);
	if ((obj = STACKL(0)) == NULL)
		goto null_pointer_exception;
	PUSHI(*(jboolean *)((char *)obj + INFO(field).u.offset));
	NEXT();
    }
TARGET(getfield_b)
    {
    	_jc_object *obj;

	POP(1);
	if ((obj = STACKL(0)) == NULL)
		goto null_pointer_exception;
	PUSHI(*(jbyte *)((char *)obj + INFO(field).u.offset));
	NEXT();
    }
TARGET(getfield_c)
    {
    	_jc_object *obj;

	POP(1);
	if ((obj = STACKL(0)) == NULL)
		goto null_pointer_exception;
	PUSHI(*(jchar *)((char *)obj + INFO(field).u.offset));
	NEXT();
    }
TARGET(getfield_s)
    {
    	_jc_object *obj;

	POP(1);
	if ((obj = STACKL(0)) == NULL)
		goto null_pointer_exception;
	PUSHI(*(jshort *)((char *)obj + INFO(field).u.offset));
	NEXT();
    }
TARGET(getfield_i)
    {
    	_jc_object *obj;

	POP(1);
	if ((obj = STACKL(0)) == NULL)
		goto null_pointer_exception;
	PUSHI(*(jint *)((char *)obj + INFO(field).u.offset));
	NEXT();
    }
TARGET(getfield_j)
    {
    	_jc_object *obj;

	POP(1);
	if ((obj = STACKL(0)) == NULL)
		goto null_pointer_exception;
	PUSHJ(*(jlong *)((char *)obj + INFO(field).u.offset));
	NEXT();
    }
TARGET(getfield_f)
    {
    	_jc_object *obj;

	POP(1);
	if ((obj = STACKL(0)) == NULL)
		goto null_pointer_exception;
	PUSHF(*(jfloat *)((char *)obj + INFO(field).u.offset));
	NEXT();
    }
TARGET(getfield_d)
    {
    	_jc_object *obj;

	POP(1);
	if ((obj = STACKL(0)) == NULL)
		goto null_pointer_exception;
	PUSHD(*(jdouble *)((char *)obj + INFO(field).u.offset));
	NEXT();
    }
TARGET(getfield_l)
    {
    	_jc_object *obj;

	POP(1);
	if ((obj = STACKL(0)) == NULL)
		goto null_pointer_exception;
	PUSHL(*(_jc_object **)((char *)obj + INFO(field).u.offset));
	NEXT();
    }
TARGET(getstatic)
    {
	_jc_field *const field = INFO(field).field;

	/* Initialize field's class */
	if (!_JC_FLG_TEST(field->class, INITIALIZED)) {
		if (_jc_initialize_type(env, field->class) != JNI_OK)
			goto exception;
	}

	/* Update instruction and execute again */
	switch (_jc_sig_types[(u_char)*field->signature]) {
	case _JC_TYPE_BOOLEAN:
		code->opcodes[pc] = _JC_getstatic_z;
		break;
	case _JC_TYPE_BYTE:
		code->opcodes[pc] = _JC_getstatic_b;
		break;
	case _JC_TYPE_CHAR:
		code->opcodes[pc] = _JC_getstatic_c;
		break;
	case _JC_TYPE_SHORT:
		code->opcodes[pc] = _JC_getstatic_s;
		break;
	case _JC_TYPE_INT:
		code->opcodes[pc] = _JC_getstatic_i;
		break;
	case _JC_TYPE_FLOAT:
		code->opcodes[pc] = _JC_getstatic_f;
		break;
	case _JC_TYPE_LONG:
		code->opcodes[pc] = _JC_getstatic_j;
		break;
	case _JC_TYPE_DOUBLE:
		code->opcodes[pc] = _JC_getstatic_d;
		break;
	case _JC_TYPE_REFERENCE:
		code->opcodes[pc] = _JC_getstatic_l;
		break;
	default:
		_JC_ASSERT(JNI_FALSE);
		break;
	}
	JUMP(pc);
    }
TARGET(getstatic_z)
	PUSHI(*(jboolean *)INFO(field).u.data);
	NEXT();
TARGET(getstatic_b)
	PUSHI(*(jbyte *)INFO(field).u.data);
	NEXT();
TARGET(getstatic_c)
	PUSHI(*(jchar *)INFO(field).u.data);
	NEXT();
TARGET(getstatic_s)
	PUSHI(*(jshort *)INFO(field).u.data);
	NEXT();
TARGET(getstatic_i)
	PUSHI(*(jint *)INFO(field).u.data);
	NEXT();
TARGET(getstatic_j)
	PUSHJ(*(jlong *)INFO(field).u.data);
	NEXT();
TARGET(getstatic_f)
	PUSHF(*(jfloat *)INFO(field).u.data);
	NEXT();
TARGET(getstatic_d)
	PUSHD(*(jdouble *)INFO(field).u.data);
	NEXT();
TARGET(getstatic_l)
	PUSHL(*(_jc_object **)INFO(field).u.data);
	NEXT();
TARGET(goto)
	JUMP(INFO(target));
TARGET(i2b)
	POP(1);
	PUSHI((jbyte)STACKI(0));
	NEXT();
TARGET(i2c)
	POP(1);
	PUSHI((jchar)STACKI(0));
	NEXT();
TARGET(i2d)
	POP(1);
	PUSHD(STACKI(0));
	NEXT();
TARGET(i2f)
	POP(1);
	PUSHF(STACKI(0));
	NEXT();
TARGET(i2l)
	POP(1);
	PUSHJ(STACKI(0));
	NEXT();
TARGET(i2s)
	POP(1);
	PUSHI((jshort)STACKI(0));
	NEXT();
TARGET(iadd)
	POP(2);
	PUSHI(STACKI(0) + STACKI(1));
	NEXT();
TARGET(iaload)
    {
	_jc_int_array *array;
	jint index;

	POP(2);
	array = (_jc_int_array *)STACKL(0);
	index = STACKI(1);
	ARRAYCHECK(array, index);
	PUSHI(array->elems[index]);
	NEXT();
    }
TARGET(iand)
	POP(2);
	PUSHI(STACKI(0) & STACKI(1));
	NEXT();
TARGET(iastore)
    {
	_jc_int_array *array;
	jint index;

	POP(3);
	array = (_jc_int_array *)STACKL(0);
	index = STACKI(1);
	ARRAYCHECK(array, index);
	array->elems[index] = STACKI(2);
	NEXT();
    }
TARGET(idiv)
	POP(2);
	if (STACKI(1) == 0)
		goto arithmetic_exception;
	PUSHI(STACKI(0) / STACKI(1));
	NEXT();
TARGET(if_acmpeq)
	POP(2);
	JUMP(STACKL(0) == STACKL(1) ? INFO(target) : pc + 1);
TARGET(if_acmpne)
	POP(2);
	JUMP(STACKL(0) != STACKL(1) ? INFO(target) : pc + 1);
TARGET(if_icmpeq)
	POP(2);
	JUMP(STACKI(0) == STACKI(1) ? INFO(target) : pc + 1);
TARGET(if_icmpne)
	POP(2);
	JUMP(STACKI(0) != STACKI(1) ? INFO(target) : pc + 1);
TARGET(if_icmplt)
	POP(2);
	JUMP(STACKI(0) < STACKI(1) ? INFO(target) : pc + 1);
TARGET(if_icmpge)
	POP(2);
	JUMP(STACKI(0) >= STACKI(1) ? INFO(target) : pc + 1);
TARGET(if_icmpgt)
	POP(2);
	JUMP(STACKI(0) > STACKI(1) ? INFO(target) : pc + 1);
TARGET(if_icmple)
	POP(2);
	JUMP(STACKI(0) <= STACKI(1) ? INFO(target) : pc + 1);
TARGET(ifeq)
	POP(1);
	JUMP(STACKI(0) == 0 ? INFO(target) : pc + 1);
TARGET(ifne)
	POP(1);
	JUMP(STACKI(0) != 0 ? INFO(target) : pc + 1);
TARGET(iflt)
	POP(1);
	JUMP(STACKI(0) < 0 ? INFO(target) : pc + 1);
TARGET(ifge)
	POP(1);
	JUMP(STACKI(0) >= 0 ? INFO(target) : pc + 1);
TARGET(ifgt)
	POP(1);
	JUMP(STACKI(0) > 0 ? INFO(target) : pc + 1);
TARGET(ifle)
	POP(1);
	JUMP(STACKI(0) <= 0 ? INFO(target) : pc + 1);
TARGET(ifnonnull)
	POP(1);
	JUMP(STACKL(0) != NULL ? INFO(target) : pc + 1);
TARGET(ifnull)
	POP(1);
	JUMP(STACKL(0) == NULL ? INFO(target) : pc + 1);
TARGET(iinc)
	LOCALI(INFO(iinc).local) += INFO(iinc).value;
	NEXT();
TARGET(iload)
	PUSHI(LOCALI(INFO(local)));
	NEXT();
TARGET(imul)
	POP(2);
	PUSHI(STACKI(0) * STACKI(1));
	NEXT();
TARGET(ineg)
	POP(1);
	PUSHI(-STACKI(0));
	NEXT();
TARGET(instanceof)
	POP(1);
	switch (_jc_instance_of(env, STACKL(0), INFO(type))) {
	case 1:
		PUSHI(1);
		break;
	case 0:
		PUSHI(0);
		break;
	case -1:
		goto exception;
	default:
		_JC_ASSERT(JNI_FALSE);
	}
	NEXT();
TARGET(invokestatic)
    {
	_jc_method *const imethod = INFO(invoke).method;

	/* Initialize method's class */
	if (!_JC_FLG_TEST(imethod->class, INITIALIZED)) {
		if (_jc_initialize_type(env, imethod->class) != JNI_OK)
			goto exception;
	}

	/* Update instruction and execute again */
	code->opcodes[pc] = _JC_invokestatic2;
	JUMP(pc);
    }
TARGET(invokespecial)
TARGET(invokevirtual)
TARGET(invokestatic2)
TARGET(invokeinterface)
    {
	const _jc_invoke *const invoke = &INFO(invoke);
	_jc_method *imethod = invoke->method;
	jint status;

	/* Sanity check */
	_JC_ASSERT((code->opcodes[pc] == _JC_invokeinterface)
	    == _JC_ACC_TEST(imethod->class, INTERFACE));

	/* Check for null and do method lookup */
	switch (code->opcodes[pc]) {
	case _JC_invokeinterface:
	    {
		_jc_method *const *methodp;
		_jc_method *quick;
		_jc_object *obj;

		/* Check for null */
		if ((obj = STACKL(-invoke->pop)) == NULL)
			goto null_pointer_exception;

		/* Verify object implements the interface */
		switch (_jc_instance_of(env, obj, imethod->class)) {
		case 0:
			_jc_post_exception_msg(env,
			    _JC_IncompatibleClassChangeError,
			    "`%s' does not implement interface `%s'",
			    obj->type->name, imethod->class->name);
			goto exception;
		case 1:
			break;
		case -1:
			goto exception;
		}

		/* Sanity check */
		_JC_ASSERT(obj->type->imethod_quick_table != NULL);
		_JC_ASSERT(obj->type->imethod_hash_table != NULL);
		_JC_ASSERT(imethod->signature_hash_bucket
		    < _JC_IMETHOD_HASHSIZE);

		/* Try quick hash table lookup */
		if ((quick = obj->type->imethod_quick_table[
		    imethod->signature_hash_bucket]) != NULL) {
		    	imethod = quick;
			goto got_method;
		}

		/* Lookup interface method entry point in hash table */
		if ((methodp = obj->type->imethod_hash_table[
		    imethod->signature_hash_bucket]) == NULL)
			goto not_found;
		do {
			_jc_method *const entry = *methodp;

			if (strcmp(entry->name, imethod->name) == 0
			    && strcmp(entry->signature,
			      imethod->signature) == 0) {
				imethod = entry;
				break;
			}
			methodp++;
		} while (*methodp != NULL);
		if (*methodp == NULL) {
not_found:		_jc_post_exception_msg(env, _JC_AbstractMethodError,
			    "%s.%s%s invoked from %s.%s%s on a %s",
			    imethod->class->name, imethod->name,
			    imethod->signature, method->class->name,
			    method->name, method->signature, obj->type->name);
			goto exception;
		}

got_method:
		/* Verify method is public */
		if (!_JC_ACC_TEST(imethod, PUBLIC)) {
			_jc_post_exception_msg(env, _JC_IllegalAccessError,
			    "%s.%s%s invoked from %s.%s%s on a %s",
			    imethod->class->name, imethod->name,
			    imethod->signature, method->class->name,
			    method->name, method->signature, obj->type->name);
			goto exception;
		}
		break;
	    }
	case _JC_invokevirtual:
	    {
		_jc_object *obj;
		_jc_type *vtype;

		/* Check for null */
		if ((obj = STACKL(-invoke->pop)) == NULL)
			goto null_pointer_exception;

		/* Resolve virtual method */
		vtype = _JC_LW_TEST(obj->lockword, ARRAY) ?
		    env->vm->boot.types.Object : obj->type;
		imethod = vtype->u.nonarray.mtable[imethod->vtable_index];
		break;
	    }
	case _JC_invokespecial:
		if (STACKL(-invoke->pop) == NULL)
			goto null_pointer_exception;
		break;
	case _JC_invokestatic2:
		break;
	default:
		_JC_ASSERT(JNI_FALSE);
		break;
	}

	/* Invoke the method */
	if (_JC_ACC_TEST(imethod, NATIVE)) {

		/* Invoke native method */
		status = _jc_invoke_native_method(env,
		    imethod, JNI_TRUE, sp - invoke->pop);

		/* Pop the stack */
		POP(invoke->pop);
	} else {

		/* Pop the stack, leaving parameters above the top */
		POP(invoke->pop);

		/* Invoke the method */
		env->sp = sp;
		status = _jc_interp(env, imethod);
		env->sp = locals + code->max_locals + code->max_stack;
	}

	/* Did method throw an exception? */
	if (status != JNI_OK)
		goto exception;

	/* Push return value, if any */
	switch (imethod->param_ptypes[imethod->num_parameters]) {
	case _JC_TYPE_BOOLEAN:
	case _JC_TYPE_BYTE:
	case _JC_TYPE_CHAR:
	case _JC_TYPE_SHORT:
	case _JC_TYPE_INT:
		PUSHI(env->retval.i);
		break;
	case _JC_TYPE_FLOAT:
		PUSHF(env->retval.f);
		break;
	case _JC_TYPE_REFERENCE:
		PUSHL(env->retval.l);
		break;
	case _JC_TYPE_LONG:
		PUSHJ(env->retval.j);
		break;
	case _JC_TYPE_DOUBLE:
		PUSHD(env->retval.d);
		break;
	case _JC_TYPE_VOID:
		break;
	default:
		_JC_ASSERT(JNI_FALSE);
	}
	NEXT();
    }
TARGET(ior)
	POP(2);
	PUSHI(STACKI(0) | STACKI(1));
	NEXT();
TARGET(irem)
	POP(2);
	if (STACKI(1) == 0)
		goto arithmetic_exception;
	PUSHI(STACKI(0) % STACKI(1));
	NEXT();
TARGET(ireturn)
	POP(1);
	env->retval.i = STACKI(0);
	goto done;
TARGET(ishl)
	POP(2);
	PUSHI(STACKI(0) << (STACKI(1) & 0x1f));
	NEXT();
TARGET(ishr)
	POP(2);
	PUSHI(_JC_ISHR(STACKI(0), STACKI(1) & 0x1f));
	NEXT();
TARGET(istore)
	POP(1);
	LOCALI(INFO(local)) = STACKI(0);
	NEXT();
TARGET(isub)
	POP(2);
	PUSHI(STACKI(0) - STACKI(1));
	NEXT();
TARGET(iushr)
	POP(2);
	PUSHI(_JC_IUSHR(STACKI(0), STACKI(1) & 0x1f));
	NEXT();
TARGET(ixor)
	POP(2);
	PUSHI(STACKI(0) ^ STACKI(1));
	NEXT();
TARGET(jsr)
	PUSHI(pc + 1);
	JUMP(INFO(target));
TARGET(l2d)
	POP2(1);
	PUSHD(STACKJ(0));
	NEXT();
TARGET(l2f)
	POP2(1);
	PUSHF(STACKJ(0));
	NEXT();
TARGET(l2i)
	POP2(1);
	PUSHI(STACKJ(0));
	NEXT();
TARGET(ladd)
	POP2(2);
	PUSHJ(STACKJ(0) + STACKJ(2));
	NEXT();
TARGET(laload)
    {
	_jc_long_array *array;
	jint index;

	POP(2);
	array = (_jc_long_array *)STACKL(0);
	index = STACKI(1);
	ARRAYCHECK(array, index);
	PUSHJ(array->elems[index]);
	NEXT();
    }
TARGET(land)
	POP2(2);
	PUSHJ(STACKJ(0) & STACKJ(2));
	NEXT();
TARGET(lastore)
    {
	_jc_long_array *array;
	jint index;

	POP(4);
	array = (_jc_long_array *)STACKL(0);
	index = STACKI(1);
	ARRAYCHECK(array, index);
	array->elems[index] = STACKJ(2);
	NEXT();
    }
TARGET(lcmp)
	POP2(2);
	PUSHI(_JC_LCMP(STACKJ(0), STACKJ(2)));
	NEXT();
TARGET(ldc)
	memcpy(sp, &INFO(constant), sizeof(_jc_word));
	POP(-1);
	NEXT();
TARGET(ldc_string)
    {
	_jc_resolve_info rinfo;
	_jc_object *string;

	/* Create intern'd string */
	if ((string = _jc_new_intern_string(env,
	    INFO(utf8), strlen(INFO(utf8)))) == NULL)
		goto exception;

	/* Create reference list with one reference */
	memset(&rinfo, 0, sizeof(rinfo));
	rinfo.loader = method->class->loader;
	rinfo.implicit_refs = &string;
	rinfo.num_implicit_refs = 1;

	/* Add implicit reference to string from class loader */
	if (_jc_merge_implicit_refs(env, &rinfo) != JNI_OK) {
		_jc_post_exception_info(env);
		goto exception;
	}

	/* Update instruction */
	INFO(constant).l = string;
	code->opcodes[pc] = _JC_ldc;

	/* Now execute it again */
	JUMP(pc);
    }
TARGET(ldc2_w)
	memcpy(sp, &INFO(constant), 2 * sizeof(_jc_word));
	POP(-2);
	NEXT();
TARGET(ldiv)
	POP2(2);
	if (STACKJ(2) == 0)
		goto arithmetic_exception;
	PUSHJ(STACKJ(0) / STACKJ(2));
	NEXT();
TARGET(lload)
	PUSHJ(LOCALJ(INFO(local)));
	NEXT();
TARGET(lmul)
	POP2(2);
	PUSHJ(STACKJ(0) * STACKJ(2));
	NEXT();
TARGET(lneg)
	POP2(1);
	PUSHJ(-STACKJ(0));
	NEXT();
TARGET(lookupswitch)
    {
	_jc_lookupswitch *const lsw = INFO(lookupswitch);
	_jc_lookup *entry;
	_jc_lookup key;

	POP(1);
	key.match = STACKI(0);
	entry = bsearch(&key, lsw->pairs, lsw->num_pairs,
	    sizeof(*lsw->pairs), _jc_lookup_compare);
	JUMP(entry != NULL ? entry->target : lsw->default_target);
    }
TARGET(lor)
	POP2(2);
	PUSHJ(STACKJ(0) | STACKJ(2));
	NEXT();
TARGET(lrem)
	POP2(2);
	if (STACKJ(2) == 0)
		goto arithmetic_exception;
	PUSHJ(STACKJ(0) % STACKJ(2));
	NEXT();
TARGET(lreturn)
	POP2(1);
	env->retval.j = STACKJ(0);
	goto done;
TARGET(lshl)
	POP(3);
	PUSHJ(STACKJ(0) << (STACKI(2) & 0x3f));
	NEXT();
TARGET(lshr)
	POP(3);
	PUSHJ(_JC_LSHR(STACKJ(0), STACKI(2)));
	NEXT();
TARGET(lstore)
	POP2(1);
	LOCALJ(INFO(local)) = STACKJ(0);
	NEXT();
TARGET(lsub)
	POP2(2);
	PUSHJ(STACKJ(0) - STACKJ(2));
	NEXT();
TARGET(lushr)
	POP(3);
	PUSHJ(_JC_LUSHR(STACKJ(0), STACKI(2)));
	NEXT();
TARGET(lxor)
	POP2(2);
	PUSHJ(STACKJ(0) ^ STACKJ(2));
	NEXT();
TARGET(monitorenter)
	POP(1);
	if (STACKL(0) == NULL)
		goto null_pointer_exception;
	if (_jc_lock_object(env, STACKL(0)) != JNI_OK)
		goto exception;
	NEXT();
TARGET(monitorexit)
	POP(1);
	if (STACKL(0) == NULL)
		goto null_pointer_exception;
	if (_jc_unlock_object(env, STACKL(0)) != JNI_OK)
		goto exception;
	NEXT();
TARGET(multianewarray)
    {
	_jc_multianewarray *const info = &INFO(multianewarray);
	_jc_array *array;
	jint *sizes;
	int i;

	POP(info->dims);
	sizes = &STACKI(0);			/* overwrite popped stack */
	for (i = 0; i < info->dims; i++)
		sizes[i] = STACKI(i);
	if ((array = _jc_new_multiarray(env,
	    info->type, info->dims, sizes)) == NULL)
		goto exception;
	PUSHL((_jc_object *)array);
	NEXT();
    }
TARGET(new)
    {
	_jc_object *obj;

	if ((obj = _jc_new_object(env, INFO(type))) == NULL)
		goto exception;
	PUSHL(obj);
	NEXT();
    }
TARGET(newarray)
    {
	_jc_array *array;

	POP(1);
	if ((array = _jc_new_array(env, INFO(type), STACKI(0))) == NULL)
		goto exception;
	PUSHL((_jc_object *)array);
	NEXT();
    }
TARGET(nop)
	NEXT();
TARGET(pop)
	POP(1);
	NEXT();
TARGET(pop2)
	POP2(1);
	NEXT();
TARGET(putfield_z)
	POP(2);
	if (STACKL(0) == NULL)
		goto null_pointer_exception;
	*(jboolean *)((char *)STACKL(0)
	    + INFO(field).u.offset) = STACKI(1) & 0x01;
	NEXT();
TARGET(putfield_b)
	POP(2);
	if (STACKL(0) == NULL)
		goto null_pointer_exception;
	*(jbyte *)((char *)STACKL(0) + INFO(field).u.offset) = STACKI(1);
	NEXT();
TARGET(putfield_c)
	POP(2);
	if (STACKL(0) == NULL)
		goto null_pointer_exception;
	*(jchar *)((char *)STACKL(0) + INFO(field).u.offset) = STACKI(1);
	NEXT();
TARGET(putfield_s)
	POP(2);
	if (STACKL(0) == NULL)
		goto null_pointer_exception;
	*(jshort *)((char *)STACKL(0) + INFO(field).u.offset) = STACKI(1);
	NEXT();
TARGET(putfield_i)
	POP(2);
	if (STACKL(0) == NULL)
		goto null_pointer_exception;
	*(jint *)((char *)STACKL(0) + INFO(field).u.offset) = STACKI(1);
	NEXT();
TARGET(putfield_j)
	POP(3);
	if (STACKL(0) == NULL)
		goto null_pointer_exception;
	*(jlong *)((char *)STACKL(0) + INFO(field).u.offset) = STACKJ(1);
	NEXT();
TARGET(putfield_f)
	POP(2);
	if (STACKL(0) == NULL)
		goto null_pointer_exception;
	*(jfloat *)((char *)STACKL(0) + INFO(field).u.offset) = STACKF(1);
	NEXT();
TARGET(putfield_d)
	POP(3);
	if (STACKL(0) == NULL)
		goto null_pointer_exception;
	*(jdouble *)((char *)STACKL(0) + INFO(field).u.offset) = STACKD(1);
	NEXT();
TARGET(putfield_l)
	POP(2);
	if (STACKL(0) == NULL)
		goto null_pointer_exception;
	*(_jc_object **)((char *)STACKL(0) + INFO(field).u.offset) = STACKL(1);
	NEXT();
TARGET(putstatic)
    {
	_jc_field *const field = INFO(field).field;

	/* Initialize field's class */
	if (!_JC_FLG_TEST(field->class, INITIALIZED)) {
		if (_jc_initialize_type(env, field->class) != JNI_OK)
			goto exception;
	}

	/* Update instruction and execute again */
	switch (_jc_sig_types[(u_char)*field->signature]) {
	case _JC_TYPE_BOOLEAN:
		code->opcodes[pc] = _JC_putstatic_z;
		break;
	case _JC_TYPE_BYTE:
		code->opcodes[pc] = _JC_putstatic_b;
		break;
	case _JC_TYPE_CHAR:
		code->opcodes[pc] = _JC_putstatic_c;
		break;
	case _JC_TYPE_SHORT:
		code->opcodes[pc] = _JC_putstatic_s;
		break;
	case _JC_TYPE_INT:
		code->opcodes[pc] = _JC_putstatic_i;
		break;
	case _JC_TYPE_FLOAT:
		code->opcodes[pc] = _JC_putstatic_f;
		break;
	case _JC_TYPE_LONG:
		code->opcodes[pc] = _JC_putstatic_j;
		break;
	case _JC_TYPE_DOUBLE:
		code->opcodes[pc] = _JC_putstatic_d;
		break;
	case _JC_TYPE_REFERENCE:
		code->opcodes[pc] = _JC_putstatic_l;
		break;
	default:
		_JC_ASSERT(JNI_FALSE);
		break;
	}
	JUMP(pc);
    }
TARGET(putstatic_z)
	POP(1);
	*(jboolean *)INFO(field).u.data = STACKI(0) & 0x01;
	NEXT();
TARGET(putstatic_b)
	POP(1);
	*(jbyte *)INFO(field).u.data = STACKI(0);
	NEXT();
TARGET(putstatic_c)
	POP(1);
	*(jchar *)INFO(field).u.data = STACKI(0);
	NEXT();
TARGET(putstatic_s)
	POP(1);
	*(jshort *)INFO(field).u.data = STACKI(0);
	NEXT();
TARGET(putstatic_i)
	POP(1);
	*(jint *)INFO(field).u.data = STACKI(0);
	NEXT();
TARGET(putstatic_j)
	POP2(1);
	*(jlong *)INFO(field).u.data = STACKJ(0);
	NEXT();
TARGET(putstatic_f)
	POP(1);
	*(jfloat *)INFO(field).u.data = STACKF(0);
	NEXT();
TARGET(putstatic_d)
	POP2(1);
	*(jdouble *)INFO(field).u.data = STACKD(0);
	NEXT();
TARGET(putstatic_l)
	POP(1);
	*(_jc_object **)INFO(field).u.data = STACKL(0);
	NEXT();
TARGET(ret)
	JUMP(LOCALI(INFO(local)));
TARGET(return)
	goto done;
TARGET(saload)
    {
	_jc_short_array *array;
	jint index;

	POP(2);
	array = (_jc_short_array *)STACKL(0);
	index = STACKI(1);
	ARRAYCHECK(array, index);
	PUSHI(array->elems[index]);
	NEXT();
    }
TARGET(sastore)
    {
	_jc_short_array *array;
	jint index;

	POP(3);
	array = (_jc_short_array *)STACKL(0);
	index = STACKI(1);
	ARRAYCHECK(array, index);
	array->elems[index] = STACKI(2);
	NEXT();
    }
TARGET(swap)
    {
	jint temp;

	temp = STACKI(-2);
	STACKI(-2) = STACKI(-1);
	STACKI(-1) = temp;
	NEXT();
    }
TARGET(tableswitch)
    {
	_jc_tableswitch *const tsw = INFO(tableswitch);
	jint key;

	POP(1);
	key = STACKI(0);
	JUMP((key >= tsw->low && key <= tsw->high) ?
	    tsw->targets[key - tsw->low] : tsw->default_target);
    }

periodic_check:
	if (_jc_thread_check(env) != JNI_OK)
		goto exception;
	ticker = PERIODIC_CHECK_TICKS;
	JUMP(pc);

null_pointer_exception:
	_jc_post_exception(env, _JC_NullPointerException);
	goto exception;

arithmetic_exception:
	_jc_post_exception(env, _JC_ArithmeticException);
	goto exception;

exception:
    {
	jint status;
	int i;

	/* Sanity check */
	_JC_ASSERT(env->pending != NULL);

	/* Check this method for a matching trap */
	for (i = 0; i < code->num_traps; i++) {
		_jc_jvm *const vm = env->vm;
		_jc_interp_trap *const trap = &code->traps[i];
		_jc_object *e;

		/* See if trap matches */
		if (pc < trap->start || pc >= trap->end)
			continue;
		if ((e = _jc_retrieve_exception(env, trap->type)) == NULL)
			continue;

		/* Verbosity for caught exception */
		if ((env->vm->verbose_flags
		    & (1 << _JC_VERBOSE_EXCEPTIONS)) != 0) {
			_jc_printf(vm, "[verbose %s: caught via trap"
			    " %d (%d-%d) in %s.%s%s in thread %p: ",
			    _jc_verbose_names[_JC_VERBOSE_EXCEPTIONS],
			    i, trap->start, trap->end,
			    method->class->name, method->name,
			    method->signature, env);
			_jc_fprint_exception_headline(env, stdout, e);
			_jc_printf(vm, "]\n");
		}

		/* Clear stack, push exception, and proceed with handler */
		sp = locals + code->max_locals;
		PUSHL(e);
		JUMP(trap->target);
	}

	/* Exception not caught */
	status = JNI_ERR;
	goto exit;

done:
	status = JNI_OK;

exit:
	/* Sanity check */
	_JC_ASSERT(status == JNI_OK || env->pending != NULL);

	/* De-synchronize if necessary */
	if (lock != NULL) {
		_jc_rvalue retval;

		/* Temporarily save return value */
		retval = env->retval;

		/* Unlock monitor */
		if (_jc_unlock_object(env, lock) != JNI_OK)
			status = JNI_ERR;

		/* Restore return value */
		env->retval = retval;
	}

	/* Pop Java stack frame */
	env->java_stack = stack_frame.next;
	env->sp = locals;

	/* Done */
	return status;
    }
}

/*
 * Comparison function for binary search of lookupswitch tables.
 */
static int
_jc_lookup_compare(const void *v1, const void *v2)
{
	const _jc_lookup *const l1 = v1;
	const _jc_lookup *const l2 = v2;

	return (l1->match > l2->match) - (l1->match < l2->match);
}

/*
 * Look up a Java source file line number.
 */
int
_jc_interp_pc_to_jline(_jc_method *method, int index)
{
	_jc_method_code *const code = &method->code;
	_jc_linemap *base;
	int span;

	/* Sanity check */
	_JC_ASSERT(_JC_FLG_TEST(method->class, RESOLVED));
	_JC_ASSERT(index >= 0 && index < code->num_insns);

	/* Binary search for line number */
	for (base = code->linemaps, span = code->num_linemaps;
	    span != 0; span >>= 1) {
		_jc_linemap *const sample = &base[span >> 1];

		if (index <= sample->index)
			continue;
		if (index > (sample + 1)->index) {
			base = sample + 1;
			span--;
			continue;
		}
		return sample->line;
	}

	/* Not found */
	return 0;
}

/*
 * Type-specific gateway functions into _jc_vinterp()
 */
#define _JC_INTERP_ENTRY(_letter, _name, _type, _rtn)			\
_type									\
_jc_ ## _name ## _ ## _letter(_jc_env *env, ...)			\
{									\
	va_list args;							\
									\
	va_start(args, env);						\
	_jc_v ## _name(env, args);					\
	va_end(args);							\
	return _rtn;							\
}
_JC_INTERP_ENTRY(z, interp, jboolean, (jboolean)env->retval.i)
_JC_INTERP_ENTRY(b, interp, jbyte, (jbyte)env->retval.i)
_JC_INTERP_ENTRY(c, interp, jchar, (jchar)env->retval.i)
_JC_INTERP_ENTRY(s, interp, jshort, (jshort)env->retval.i)
_JC_INTERP_ENTRY(i, interp, jint, env->retval.i)
_JC_INTERP_ENTRY(j, interp, jlong, env->retval.j)
_JC_INTERP_ENTRY(f, interp, jfloat, env->retval.f)
_JC_INTERP_ENTRY(d, interp, jdouble, env->retval.d)
_JC_INTERP_ENTRY(l, interp, _jc_object *, env->retval.l)
_JC_INTERP_ENTRY(v, interp, void, )
_JC_INTERP_ENTRY(z, interp_native, jboolean, (jboolean)env->retval.i)
_JC_INTERP_ENTRY(b, interp_native, jbyte, (jbyte)env->retval.i)
_JC_INTERP_ENTRY(c, interp_native, jchar, (jchar)env->retval.i)
_JC_INTERP_ENTRY(s, interp_native, jshort, (jshort)env->retval.i)
_JC_INTERP_ENTRY(i, interp_native, jint, env->retval.i)
_JC_INTERP_ENTRY(j, interp_native, jlong, env->retval.j)
_JC_INTERP_ENTRY(f, interp_native, jfloat, env->retval.f)
_JC_INTERP_ENTRY(d, interp_native, jdouble, env->retval.d)
_JC_INTERP_ENTRY(l, interp_native, _jc_object *, env->retval.l)
_JC_INTERP_ENTRY(v, interp_native, void, )

/*
 * Entry point for interpreted methods when invoked from JCNI methods.
 * We get here by way of a trampoline set up by _jc_build_trampoline(),
 * which also sets env->interp to the method we are going to interpret.
 */
static void
_jc_vinterp(_jc_env *env, va_list args)
{
#ifndef NDEBUG
	const jboolean was_interpreting = env->interpreting;
#endif
	jboolean allocated_stack = JNI_FALSE;
	_jc_method *const method = env->interp;
	_jc_word *sp;
	jint status;
	int i;

	/* Sanity check */
	_JC_ASSERT(!_JC_ACC_TEST(method, NATIVE));
	_JC_ASSERT(_JC_FLG_TEST(method->class, RESOLVED));

	/* For a brand new thread, allocate its Java stack here */
	if (env->sp == NULL) {
		_jc_jvm *const vm = env->vm;

		/* Sanity check */
		_JC_ASSERT(env->stack_data == NULL);
		_JC_ASSERT(env->stack_data_end == NULL);

		/* Allocate Java stack */
		if ((env->stack_data = _jc_vm_alloc(env,
		    vm->java_stack_size * sizeof(*env->stack_data))) == NULL) {
			_jc_post_exception_info(env);
			_jc_throw_exception(env);
		}
		env->stack_data_end = env->stack_data
		    + vm->java_stack_size - _JC_JAVA_STACK_MARGIN;
		env->sp = env->stack_data;
		allocated_stack = JNI_TRUE;
	}

	/* Place paramters over top of the stack like _jc_interp() expects */
	sp = env->sp;

	/* Check Java stack overflow; release secret space during exception */
	if (sp + 1 + method->code.num_params2 > env->stack_data_end) {
		if ((env->in_vmex & (1 << _JC_StackOverflowError)) == 0
		    || sp + 1 + method->code.num_params2
		      > env->stack_data_end + _JC_JAVA_STACK_MARGIN) {
			_jc_post_exception(env, _JC_StackOverflowError);
			_jc_throw_exception(env);
		}
	}

	/* Push 'this' if method is non-static */
	if (!_JC_ACC_TEST(method, STATIC)) {
		_jc_object *const this = va_arg(args, _jc_object *);

		_JC_ASSERT(this != NULL);
		*sp++ = (_jc_word)this;
	}

	/* Push method parameters, occupying two slots for long/double */
	for (i = 0; i < method->num_parameters; i++) {
		switch (method->param_ptypes[i]) {
		case _JC_TYPE_BOOLEAN:
			*sp++ = (jint)(jboolean)va_arg(args, jint);
			break;
		case _JC_TYPE_BYTE:
			*sp++ = (jint)(jbyte)va_arg(args, jint);
			break;
		case _JC_TYPE_CHAR:
			*sp++ = (jint)(jchar)va_arg(args, jint);
			break;
		case _JC_TYPE_SHORT:
			*sp++ = (jint)(jshort)va_arg(args, jint);
			break;
		case _JC_TYPE_INT:
			*sp++ = va_arg(args, jint);
			break;
		case _JC_TYPE_FLOAT:
		    {
			jfloat param = (jfloat)va_arg(args, jdouble);

			memcpy(sp, &param, sizeof(param));
			sp++;
			break;
		    }
		case _JC_TYPE_LONG:
		    {
			jlong param = va_arg(args, jlong);

			memcpy(sp, &param, sizeof(param));
			sp += 2;
			break;
		    }
		case _JC_TYPE_DOUBLE:
		    {
			jdouble param = va_arg(args, jdouble);

			memcpy(sp, &param, sizeof(param));
			sp += 2;
			break;
		    }
		case _JC_TYPE_REFERENCE:
			*sp++ = (_jc_word)va_arg(args, _jc_object *);
			break;
		default:
			_JC_ASSERT(JNI_FALSE);
			break;
		}
	}
	_JC_ASSERT(sp - env->sp == method->code.num_params2
	    + !_JC_ACC_TEST(method, STATIC));

#ifndef NDEBUG
        /* Signals are not OK now */
	env->interpreting = JNI_TRUE;
#endif

	/* Invoke method */
	status = _jc_interp(env, method);

#ifndef NDEBUG
        /* Restore debug flag */
	env->interpreting = was_interpreting;
#endif

	/* Free Java stack */
	if (allocated_stack) {
		_JC_ASSERT(env->sp == env->stack_data);
		_jc_vm_free(&env->stack_data);
		env->stack_data_end = NULL;
		env->sp = NULL;
	}

	/* Throw exception if any */
	if (status != JNI_OK)
		_jc_throw_exception(env);
}

/*
 * Same thing as _jc_vinterp() but for native methods.
 */
static void
_jc_vinterp_native(_jc_env *env, va_list args)
{
	_jc_method *const method = env->interp;

	/* Sanity check */
	_JC_ASSERT(_JC_ACC_TEST(method, NATIVE));
	_JC_ASSERT(_JC_FLG_TEST(method->class, RESOLVED));

	/* Invoke method */
	if (_jc_invoke_native_method(env, method, JNI_FALSE, args) != JNI_OK)
		_jc_throw_exception(env);
}

