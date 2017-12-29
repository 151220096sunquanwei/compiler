//
// Created by sunxudong on 12/27/17.
//

#include <symbol_table.h>
#include <stdatomic.h>
#include "../common.h"
#include "../intercode/IC.h"
#include "../intercode/ICTable.h"
#include "../intercode/InterCode.h"
#include "spim.h"

int VSize;
int TSize;
int VBegin;
int TBegin;
int* VOffset;
int* TOffset;

AIRE* AIRV;
AISE* AISV;

void InitRootInterCodeList()
{
    InterCodeEntry *ICVhead, *ICVtail;
    ICVhead = RootInterCodeList->head;
    ICVtail = ICVhead->prev;

    ICVhead->prev = NULL;
    ICVtail->next = NULL;

    AIRV = (AIRE*)malloc(sizeof(AIRE)*4);
    AISV = (AISE*)malloc(sizeof(AISV)*100);
}

void GetVTSize()
{
    TSize = TIndex;
    VSize = 0;
    ICVarEntry* ICV;
    for(ICV = RootICVarTable->head; ICV != NULL; ICV = ICV->next)
    {
        Type* TP = ICV->VariableType;
        if(TP->kind == BASIC)VSize++;
        else if(TP->kind == ARRAY)
        {
            assert(TP->array.dim == 1);
            VSize = VSize + TP->array.DimSize[0];
        }
        else assert(0);
    }

    VOffset = (int*)malloc((unsigned)4*VIndex);
    TOffset = (int*)malloc((unsigned)4*TIndex);

    int i;
    int offset = 0;
    VOffset[0] = 0;
    for(ICV = RootICVarTable->head; ICV->next != NULL; ICV = ICV->next)
    {
        ICVarEntry* ICVnxt = ICV->next;
        Type* TP = ICV->VariableType;
        if(TP->kind == BASIC)
        {
            offset++;
            VOffset[ICVnxt->VIndex] = offset;
        }
        else if(TP->kind == ARRAY)
        {
            assert(TP->array.dim == 1);
            offset = offset + TP->array.DimSize[0];
            VOffset[ICVnxt->VIndex] = offset;
        }
        else assert(0);
    }

    offset = 0;
    for(i = 0; i<TIndex; i++)
    {
        TOffset[i] = offset;
        offset++;
    }

    VBegin = 0;                 // v_i is at ( $gp + ( VBegin + VOffset[i]*4 )) in stack
    TBegin = VBegin + VSize*4;  // t_i is at ( $gp + ( TBegin + TOffset[i]*4 )) in stack

    assert(TSize + VSize <= 8*1024);


    /* now in static data segment(64K):
     *
     *         --------------  high address : 0x10010000
     *         |   T data   |
     *         --------------
     *         |   V data   |
     *         --------------  <- $gp : 0x10008000 (points to the middle of first 64K DATA SEGMENT)
     *         |            |
     *         |            |
     *         |            |
     *         --------------  low address : 0x10000000
     */



}

int GetVTAddrRelGP(Operand* op)
{
    if(op->kind == OVAR)return VBegin+4*VOffset[op->VIndex];
    else if(op->kind == OTEMP)return  TBegin+4*TOffset[op->TIndex];
    else assert(0);
}

void MachineCodePreparation(FILE* stream)
{

    fprintf(stream,
            ".data\n"
            "_prompt: .asciiz \"Enter an integer:\"\n"
            "_ret: .asciiz \"\\n\"\n"
            ".globl main\n"
            ".text\n"
            "read:\n"
            "  li $v0, 4\n"
            "  la $a0, _prompt\n"
            "  syscall\n"
            "  li $v0, 5\n"
            "  syscall\n"
            "  jr $ra\n\n"
            "write:\n"
            "  li $v0, 1\n"
            "  syscall\n"
            "  li $v0, 4\n"
            "  la $a0, _ret\n"
            "  syscall\n"
            "  move $v0, $0\n"
            "  jr $ra\n");
}

void FUNGenerator(InterCode* IC, FILE* stream, int argNum)
{
    assert(IC->kind == IFUNCTION);

    fprintf(stream, "\n%s:\n", IC->FUN.funName);

    if(strcmp(IC->FUN.funName, "main") == 0)
    {
        assert(argNum == 0);

        fprintf(stream, "  subu $sp, $sp, 8\n");
        fprintf(stream, "  sw $fp, (0)($sp)\n");
        fprintf(stream, "  addi $fp, $sp, 8\n");

        /* now in main function:
         *
         *                         high address
         *         --------------  <- $fp
         *         |   blank    |
         *         --------------
         *         |   old fp   |
         *         --------------  <- $sp
         *                         low address
         */
    }
    else
    {
        fprintf(stream, "  subu $sp, $sp, 8\n");
        fprintf(stream, "  sw $ra, (4)($sp)\n");
        fprintf(stream, "  sw $fp, (0)($sp)\n");
        fprintf(stream, "  addi $fp, $sp, 8\n");

        assert(0);
        // To-Do: load args into static data segment

       /* now in xxx function:
        *                         high address
        *         --------------
        *         |            |
        *         |    args    |
        *         |            |
        *         --------------  <- $fp
        *         |  ret addr  |
        *         --------------
        *         |   old fp   |
        *         --------------  <- $sp
        *                         low address
        */

    }
}


InterCodeEntry* ParamFunGenerator(InterCodeEntry* ICV, FILE* stream)
{
    assert(ICV->IC->kind == IPARAM);

    InterCodeEntry* entry;
    int argNum = 0;
    for( entry = ICV;entry->IC->kind==IPARAM; entry = entry->next)
    {
        if(argNum<=3)
        {
            Operand* op = entry->IC->PARAM.parameter;
            AIRV[argNum].kind = op->kind;
            assert(op->kind == OVAR);
            AIRV[argNum].Index = op->VIndex;
        }
        else
        {
            Operand* op = entry->IC->PARAM.parameter;
            AISV[argNum - 4].kind = op->kind;
            assert(op->kind == OVAR);
            AISV[argNum - 4].Index = op->VIndex;
        }
        argNum++;
    }

    assert(entry->IC->kind == IFUNCTION);


    FUNGenerator(entry->IC, stream, argNum);

    return entry;


}

void AssignGenerator(InterCode* IC, FILE* stream)
{
    assert(IC->kind == IASSIGN);

    Operand *left, *right;
    left = IC->ASSIGN.left;
    right = IC->ASSIGN.right;
    if(right->kind == OICONS)
    {
        int leftAddr = GetVTAddrRelGP(left);

        fprintf(stream, "  li $t0, %d\n", right->ICons);
        fprintf(stream, "  sw $t0, %d($gp)\n", leftAddr);
    }
    else
    {
        int rightAddr = GetVTAddrRelGP(right);
        int leftAddr = GetVTAddrRelGP(left);

        if(right->attr == OREF)
        {
            assert(0);
            fprintf(stream, "  lw $t0, %d($gp)\n", rightAddr);
            fprintf(stream, "  lw $t0, (0)($t0)\n");

        }
        else if(right->attr == OADDR)
        {
            assert(0);
            fprintf(stream, "  move $t0, %d\n", rightAddr);
            fprintf(stream, "  addi $t0, $t0, %x\n", 0x10008000);
        }
        else
        {
            fprintf(stream, "  lw $t0, %d($gp)\n", rightAddr);
        }

        if(left->attr == OREF)
        {
            assert(0);
            fprintf(stream, "  lw $t1, %d($gp)\n", leftAddr);
            fprintf(stream, "  sw $t0, (0)($t1)\n");

        }
        else
        {
            fprintf(stream, "  sw $t0, %d($gp)\n", leftAddr);
        }



    }
}

void READGenerator(InterCode* IC, FILE* stream)
{
    assert(IC->kind == IREAD);

    fprintf(stream,
            "  addi $sp, $sp, -4\n"
            "  sw $ra, 0($sp)\n"
            "  jal read\n"
            "  lw $ra, 0($sp)\n"
            "  addi $sp, $sp, 4\n"
            "  move $t0, $v0\n");

    Operand* input = IC->READ.input;
    int inputAddr = GetVTAddrRelGP(input);
    fprintf(stream, "  sw $t0, %d($gp)\n", inputAddr);
}

void WRITEGenerator(InterCode* IC, FILE* stream)
{
    assert(IC->kind == IWRITE);

    Operand* ouput = IC->WRITE.output;
    int outputAddr = GetVTAddrRelGP(ouput);
    fprintf(stream, "  lw $t0, %d($gp)\n", outputAddr);
    fprintf(stream,
            "  move $a0, $t0\n"
            "  addi $sp, $sp, -4\n"
            "  sw $ra, 0($sp)\n"
            "  jal write\n"
            "  lw $ra, 0($sp)\n"
            "  addi $sp, $sp, 4");
}

void ReturnGenerator(InterCode* IC, FILE* stream)
{
    assert(IC->kind == IRETURN);

    Operand* ret = IC->RET.ret;

    if(ret->kind == OICONS)
    {
        fprintf(stream, "  li $v0, %d\n", ret->ICons);
    }
    else
    {
        int retAddr = GetVTAddrRelGP(ret);
        fprintf(stream, "  lw $v0, %d($gp)\n", retAddr);
    }
    fprintf(stream, "  jr $ra");



}

void LABELGenerator(InterCode* IC, FILE* stream)
{
    assert(IC->kind == ILABEL);

    fprintf(stream, "label%d:\n", IC->LABELDEC.LIndex);
}

void IFGOTOGenerator(InterCode* IC, FILE* stream)
{
    assert(IC->kind == IIFGOTO);

    Operand *op1, *op2;
    op1 = IC->IFGT.condition->op1;
    op2 = IC->IFGT.condition->op2;

    int op1Addr, op2Addr;
    op1Addr = GetVTAddrRelGP(op1);
    op2Addr = GetVTAddrRelGP(op2);

    fprintf(stream, "  lw $t0, %d($gp)\n", op1Addr);
    fprintf(stream, "  lw $t1, %d($gp)\n", op2Addr);

    switch (IC->IFGT.condition->relop)
    {
        case EQ:fprintf(stream, "  beq ");break;
        case LT:fprintf(stream, "  blt ");break;
        case GT:fprintf(stream, "  bgt ");break;
        case NEQ:fprintf(stream, "  bne ");break;
        case LEQ:fprintf(stream, "  ble ");break;
        case GEQ:fprintf(stream, "  bge ");break;
        default:assert(0);
    }

    fprintf(stream, "$t0, $t1, label%d\n", IC->IFGT.LIndex);

}

void GOTOGenerator(InterCode* IC, FILE* stream)
{
    assert(IC->kind == IGOTO);

    fprintf(stream, "  j label%d\n", IC->GT.LIndex);
}

void BINOPGenerator(InterCode* IC, FILE* stream)
{
    assert(IC->kind == IADD
           || IC->kind == ISUB
           || IC->kind == IMUL
           || IC->kind == IDIV);

    assert(IC->BINOP.result->kind == OTEMP || IC->BINOP.result->kind == OVAR);
    assert(IC->BINOP.result->attr == OVALUE
           && IC->BINOP.op1->attr == OVALUE
           && IC->BINOP.op2->attr == OVALUE);

    Operand *op1, *op2, *result;
    op1 = IC->BINOP.op1;
    op2 = IC->BINOP.op2;
    result = IC->BINOP.result;

    int op1Addr, op2Addr, resultAddr;
    resultAddr = GetVTAddrRelGP(result);

    if(op1->kind == OICONS)
    {
        fprintf(stream, "  move $t0, %d\n", op1->ICons);
    }
    else
    {
        op1Addr = GetVTAddrRelGP(op1);
        fprintf(stream, "  lw $t0, %d($gp)\n", op1Addr);
    }

    if(op2->kind == OICONS)
    {
        fprintf(stream, "  move $t1, %d\n", op2->ICons);
    }
    else
    {
        op2Addr = GetVTAddrRelGP(op2);
        fprintf(stream, "  lw $t1, %d($gp)\n", op2Addr);
    }

    switch (IC->kind)
    {
        case IADD:fprintf(stream, "  add $t2, $t0, $t1\n");break;
        case ISUB:fprintf(stream, "  sub $t2, $t0, $t1\n");break;
        case IMUL:fprintf(stream, "  mul $t2, $t0, $t1\n");break;
        case IDIV:fprintf(stream, "  div $t0, $t1\n  mflo $t2\n");break;
        default:assert(0);
    }

    fprintf(stream, "  sw $t2, %d($gp)\n", resultAddr);

}

void MachineCodeGenerator(char* filename)
{
    FILE* fp;

    if(filename == NULL)
    {
        fp = stdout;
    }
    else
    {
        fp = fopen(filename, "w");
        assert(fp != NULL);
    }

    InitRootInterCodeList();
    MachineCodePreparation(fp);
    GetVTSize();


    InterCodeEntry* ICV ;
    for (ICV = RootInterCodeList->head;  ICV->next!=NULL ; ICV = ICV->next)
    {
        InterCode* IC = ICV->IC;
        switch (IC->kind)
        {
            case IFUNCTION: FUNGenerator(IC, fp, 0); break;
            case IPARAM: ICV = ParamFunGenerator(ICV, fp); break;
            case IASSIGN: AssignGenerator(IC, fp); break;
            case IREAD:READGenerator(IC, fp);break;
            case IWRITE:WRITEGenerator(IC,fp);break;
            case ILABEL:LABELGenerator(IC, fp);break;
            case IIFGOTO:IFGOTOGenerator(IC, fp);break;
            case IGOTO:GOTOGenerator(IC, fp);break;
            case IADD:
            case ISUB:
            case IMUL:
            case IDIV:BINOPGenerator(IC, fp);break;
            case IRETURN:ReturnGenerator(IC, fp);break;
            default:assert(0);
        }
    }
    fprintf(fp, "\n");

}