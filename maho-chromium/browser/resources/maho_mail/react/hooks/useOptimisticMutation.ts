import { useCallback, useState, useRef } from "react";
import { useToast } from "../components/ui/Toast";

export interface OptimisticMutationOptions<TVariables, TData> {
  mutate: (variables: TVariables) => Promise<TData>;
  applyOptimistic: (variables: TVariables) => void;
  rollback: (variables: TVariables, error: Error) => void;
  onError?: (error: Error, variables: TVariables) => void;
  onSuccess?: (data: TData, variables: TVariables) => void;
}

export function useOptimisticMutation<TVariables = void, TData = void>(
  options: OptimisticMutationOptions<TVariables, TData>
) {
  const [isLoading, setIsLoading] = useState(false);
  const [error, setError] = useState<Error | null>(null);
  const { toast } = useToast();

  const optionsRef = useRef(options);
  optionsRef.current = options;

  const execute = useCallback(
    async (variables: TVariables): Promise<TData | undefined> => {
      setIsLoading(true);
      setError(null);

      // 1. Apply optimistic update immediately
      try {
        optionsRef.current.applyOptimistic(variables);
      } catch (err) {
        console.error("Failed to apply optimistic update:", err);
      }

      // 2. Perform background mutation
      try {
        const data = await optionsRef.current.mutate(variables);
        optionsRef.current.onSuccess?.(data, variables);
        return data;
      } catch (err) {
        const actualError = err instanceof Error ? err : new Error(String(err));
        setError(actualError);

        // 3. Roll back state on failure
        try {
          optionsRef.current.rollback(variables, actualError);
        } catch (rollbackErr) {
          console.error("Failed to rollback optimistic update:", rollbackErr);
        }

        // 4. Show error toast
        toast("error", `Action failed: ${actualError.message}`);
        optionsRef.current.onError?.(actualError, variables);
        return undefined;
      } finally {
        setIsLoading(false);
      }
    },
    [toast]
  );

  return {
    execute,
    isLoading,
    error,
  };
}
