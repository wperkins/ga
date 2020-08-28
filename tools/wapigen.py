#!/usr/bin/env python

'''Generate the ga-wapi.c source from the ga-papi.h header.'''

import sys

def get_signatures(header):
    # first, gather all function signatures from ga-papi.h aka argv[1]
    accumulating = False
    #ALL INDIVIDUAL SIGNATURES COLLECTED HERE
    signatures = []
    
    #MULTI LINE SIGNATURES GATHERED HERE
    current_signature = ''
    
    #IDENTIFIABLE SYMBOLS
    EXTERN = 'extern'
    SEMICOLON = ';'
    
    for line in open(header):
        line = line.strip() # remove whitespace before and after line
        if not line:
            continue # skip blank lines
        if EXTERN in line and SEMICOLON in line:
            signatures.append(line) #"EXTERN "CONTENT GOES HERE";" IS THE SIGNATURE
        elif EXTERN in line:
            current_signature = line
            accumulating = True #THE SIGNATURE ISN'T COMPLETE YET
        elif SEMICOLON in line and accumulating:
            current_signature += line
            signatures.append(current_signature) #SIGNATURE COMPLETE
            accumulating = False 
        elif accumulating:
            current_signature += line #KEEP GATHERING WHOLE SIGNATURE
    return signatures

class FunctionArgument(object): #OBJECT A SINGLE ARGUMENT
    def __init__(self, signature): 
        self.pointer = signature.count('*') 
        self.array = '[' in signature #TRUE OR FALSE IF IT'S AN ARRAY OR NOT
        signature = signature.replace('*','').strip() #REMOVE *
        signature = signature.replace('[','').strip() #REMOVE [
        signature = signature.replace(']','').strip() #REMOVE ]
        self.type,self.name = signature.split() #TYPE AND NAME IDENTIFIED

    def __str__(self):
        ret = self.type[:]
        ret += ' '
        for p in range(self.pointer):
            ret += '*'
        ret += self.name
        if self.array:
            ret += '[]'
        return ret

class Function(object): #OBJECT A SINGLE SIGNATURE
    def __init__(self, signature):
        signature = signature.replace('extern','').strip() #REMOVE EXTERNS FROM SIGNATURE / REMOVE WHITESPACE BEFORE AND AFTER LINE

        self.return_type,signature = signature.split(None,1)  #SEPERATES THE RETURN FROM THE REST OF THE SIGNATURE
        self.return_type = self.return_type.strip() #REMOVE END WHITE SPACES
        
        signature = signature.strip() #REMOVE END WHITE SPACES
        self.name,signature = signature.split('(',1) #SEPERATES THE FUNCTION NAME FROM THE REST OF THE SIGNATURE
        self.name = self.name.strip() #REMOVE END WHITE SPACES
        signature = signature.replace(')','').strip() #REMOVE PARENTHESIS
        signature = signature.replace(';','').strip() #REMOVE SEMICOLONS
        self.args = [] #ARRAY TO STORE ARGUMENTS
        if signature: #IF SIGNATURE ISN'T EMPTY?
            for arg in signature.split(','):
                self.args.append(FunctionArgument(arg.strip())) #TAKE EACH COMMA SEPERATED VALUE AS A FUNCTIONARGUMENT AND STORE

    def get_call(self, name=None): #NO NAME SENT
        sig = ''
        if not name:
            sig += self.name
        else:
            sig += name
        sig += '('
        if self.args:
            for arg in self.args:
                sig += arg.name
                sig += ', '
            sig = sig[:-2] # remove last ', '
        sig += ')'
        return sig

    def get_signature(self, name=None): #ADDS IDENTIFIER AND RECONSTRUCTS SIGNATURE
        sig = self.return_type[:]
        sig += ' '
        if not name:
            sig += self.name
        else:
            sig += name
        sig += '('
        if self.args:
            for arg in self.args:
                sig += str(arg)
                sig += ', '
            sig = sig[:-2] # remove last ', '
        sig += ')'
        return sig
        
    def get_hash_key(self, name=None):
       sig = ''
       if not name:
            sig += self.name
       else:
            sig += name  
       return sig     

    def __str__(self):
        return self.get_signature()



#MAIN STARTS HERE
if __name__ == '__main__':
    #USES SPECIFIC ARGUMENTS TO RUN
    if len(sys.argv) != 2:
        print 'incorrect number of arguments'
        print 'usage: wapigen.py <ga-papi.h> > <ga-wapi.c>'
        sys.exit(len(sys.argv))

    # print headers
    print '''
#if HAVE_CONFIG_H
#   include "config.h"
#endif
#include "ga-papi.h"
#include "typesf2c.h"
'''

    #DICTIONARY OF FUNCTIONS
    functions = {}
    
    # parse signatures into the Function class
    #SIGNATURES BEING GOT FROM ga-papi.h
    for sig in get_signatures(sys.argv[1]):
        #PLACE SINGLE SIGNATURE INTO FUNCTION
        function = Function(sig)
        #PLACE FUNCTION WITH SIGNATURE INTO FUNCTION DICTIONARY
        functions[function.name] = function
  

    # now process the functions
    for name in sorted(functions):
        
        func = functions[name] #FEC5
        
        #DOES IT NEED RETURN COMMAND
        maybe_return = ''
        maybe_rvalue = ''
        if 'void' not in func.return_type:
            maybe_return = 'return rvalue;'
            maybe_rvalue = func.return_type
            maybe_rvalue += ' rvalue = '
        
        #ADD IDENTIFIER AT BEGINNING OF NAME
        func = functions[name]
        wnga_name = name.replace('pnga_','wnga_')
        hash_key = func.get_hash_key()
        
        start_nperf = "add_entry(GA_Nodeid(), GA_Nodeid(), \"%s\")" %(hash_key)
        end_nperf = "end_entry(GA_Nodeid(), GA_Nodeid(), \"%s\")" %(hash_key)           
        
        print '''
%s
{
    %s;
    %s%s;
    %s;
    %s
}
''' % (func.get_signature(wnga_name), start_nperf, maybe_rvalue, func.get_call(), end_nperf, maybe_return)

#THIS PRINTS OUT THE FUNCTION FOR THE C FILE

#logical wnga_allocate(Integer g_a) 
#{
#   return allocate(Integer g_a);
#}

#void wnga_add_diagonal (Integer g_a, Integer g_v)
#{
#  void add_diagonal(Integer g_a, Integer g_v)
#}
#
#
#
#
#

# FUNCTION
# RETURN TYPE: logical
# NAME: allocate
# ARGS [FUNCTION ARRAYS]
# 
# FUNCTION ARGUMENT [0]
# POINTER: 0
# ARRAY: 0
# TYPE: Integer
# NAME: g_a




